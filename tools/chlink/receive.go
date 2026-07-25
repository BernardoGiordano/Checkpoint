package main

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math/big"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"sync"
	"time"
)

type receiveOpts struct {
	pin       string
	outDir    string
	flat      bool
	keepZip   bool
	noExtract bool
	once      bool
	verbose   bool
	jsonOut   bool
}

// receiver implements the console's /transfer/info and /transfer/upload
// endpoints with the same validation: constant-time PIN compare, zip
// path-traversal rejection, CRC verification (via archive/zip), and
// same-name backup folder replacement. Upload bodies stream to a temp file
// in outDir, never RAM.
type receiver struct {
	opts      receiveOpts
	mu        sync.Mutex // serializes uploads, like the console's single-threaded server
	onSuccess func()
}

func cmdReceive(args []string) error {
	fs := flag.NewFlagSet("receive", flag.ExitOnError)
	c := addCommon(fs)
	pin := fs.String("pin", "", "fixed PIN (4 digits); auto-generated when omitted")
	out := fs.String("out", "./received", "output directory")
	flat := fs.Bool("flat", false, "store straight into <out>/<backupName> (no type/title tree)")
	keepZip := fs.Bool("keep-zip", false, "keep the received archive next to the extracted folder")
	noExtract := fs.Bool("no-extract", false, "store the zip only, do not extract")
	once := fs.Bool("once", false, "exit after the first successful upload")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: chlink receive [flags]")
		fs.PrintDefaults()
	}
	fs.Parse(args)
	if fs.NArg() != 0 {
		fs.Usage()
		return fmt.Errorf("receive takes no positional arguments")
	}

	pinStr := *pin
	if pinStr == "" {
		n, err := rand.Int(rand.Reader, big.NewInt(10000))
		if err != nil {
			return err
		}
		pinStr = fmt.Sprintf("%04d", n.Int64())
	}
	if !validPin(pinStr) {
		return fmt.Errorf("PIN must be exactly 4 digits")
	}

	outDir, err := filepath.Abs(*out)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}
	sweepTempFiles(outDir, c.verbose)

	rv := &receiver{opts: receiveOpts{
		pin:       pinStr,
		outDir:    outDir,
		flat:      *flat,
		keepZip:   *keepZip,
		noExtract: *noExtract,
		once:      *once,
		verbose:   c.verbose,
		jsonOut:   c.jsonOut,
	}}

	done := make(chan struct{})
	var onceGuard sync.Once
	if *once {
		rv.onSuccess = func() { onceGuard.Do(func() { close(done) }) }
	}

	srv := &http.Server{
		Addr:    ":" + strconv.Itoa(c.port),
		Handler: rv.handler(),
	}

	if c.jsonOut {
		json.NewEncoder(os.Stdout).Encode(map[string]any{
			"event": "listening", "port": c.port, "pin": pinStr, "ips": localIPv4s(), "out": outDir,
		})
	} else {
		fmt.Printf("chlink %s — receiving into %s\n\n", version, outDir)
		for _, ip := range localIPv4s() {
			fmt.Printf("  address: %s:%d\n", ip, c.port)
		}
		fmt.Printf("\n  PIN:  %s\n\n", pinStr)
		fmt.Println("waiting for uploads (Ctrl-C to stop)...")
	}

	sigc := make(chan os.Signal, 1)
	signal.Notify(sigc, os.Interrupt)
	go func() {
		select {
		case <-sigc:
			if !c.jsonOut {
				fmt.Fprintln(os.Stderr, "\nshutting down...")
			}
		case <-done:
		}
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		srv.Shutdown(ctx)
	}()

	err = srv.ListenAndServe()
	sweepTempFiles(outDir, false)
	if err == http.ErrServerClosed {
		return nil
	}
	return err
}

// sweepTempFiles removes spool files left behind by a crash or Ctrl-C
// mid-upload (parity with the console's boot sweep).
func sweepTempFiles(outDir string, verbose bool) {
	matches, _ := filepath.Glob(filepath.Join(outDir, "chlink_recv_*"))
	for _, m := range matches {
		if verbose {
			fmt.Fprintf(os.Stderr, "removing leftover %s\n", m)
		}
		os.Remove(m)
	}
}

func (rv *receiver) handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/transfer/info", rv.handleInfo)
	mux.HandleFunc("/transfer/upload", rv.handleUpload)
	return mux
}

func (rv *receiver) handleInfo(w http.ResponseWriter, r *http.Request) {
	// The PIN is deliberately never exposed here; it travels out-of-band.
	writeJSON(w, http.StatusOK, InfoResponse{
		Device:  "PC",
		Version: version,
		// The CLI streams uploads to disk, so it has no fixed cap: 0 = unlimited.
		MaxUploadBytes: 0,
		FreeSpaceBytes: 0,
	})
}

func (rv *receiver) handleUpload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		drainBody(r)
		writeJSON(w, http.StatusBadRequest, UploadResponse{OK: false, Error: "Bad upload"})
		return
	}
	token := r.Header.Get("X-CP-Token")
	if subtle.ConstantTimeCompare([]byte(token), []byte(rv.opts.pin)) != 1 {
		// Drain the body before answering, like the console server (which
		// buffers the whole request): responding mid-upload makes senders see
		// a connection reset instead of the 403.
		drainBody(r)
		writeJSON(w, http.StatusForbidden, UploadResponse{OK: false, Error: "Invalid token"})
		return
	}

	rv.mu.Lock()
	defer rv.mu.Unlock()

	savedPath, meta, err := rv.storeUpload(r)
	if err != nil {
		drainBody(r)
		rv.logEvent(map[string]any{"event": "upload_failed", "error": err.Error()})
		writeJSON(w, http.StatusBadRequest, UploadResponse{OK: false, Error: err.Error()})
		return
	}
	rv.logEvent(map[string]any{
		"event": "received", "savedPath": savedPath,
		"titleId": meta.TitleID, "titleName": meta.TitleName,
		"dataType": meta.DataType, "backupName": meta.BackupName,
	})
	writeJSON(w, http.StatusOK, UploadResponse{OK: true, SavedPath: savedPath})
	if rv.onSuccess != nil {
		rv.onSuccess()
	}
}

// storeUpload streams the multipart body, spools the file part to a temp
// file, and lands the backup under the mirrored console layout:
// <out>/<type>/<titleId titleName>/<backupName>/ (or <out>/<backupName>
// with --flat).
func (rv *receiver) storeUpload(r *http.Request) (string, *Meta, error) {
	mr, err := r.MultipartReader()
	if err != nil {
		return "", nil, fmt.Errorf("bad multipart body: %v", err)
	}

	var meta *Meta
	var spool string
	var spoolBytes int64
	defer func() {
		if spool != "" {
			os.Remove(spool)
		}
	}()

	for {
		part, err := mr.NextPart()
		if err == io.EOF {
			break
		}
		if err != nil {
			return "", nil, fmt.Errorf("bad multipart body: %v", err)
		}
		switch part.FormName() {
		case "meta":
			var m Meta
			if err := json.NewDecoder(io.LimitReader(part, 1<<20)).Decode(&m); err != nil {
				return "", nil, fmt.Errorf("invalid meta: %v", err)
			}
			meta = &m
		case "file":
			tmp, err := os.CreateTemp(rv.opts.outDir, "chlink_recv_*")
			if err != nil {
				return "", nil, err
			}
			spool = tmp.Name()
			spoolBytes, err = io.Copy(tmp, part)
			if cerr := tmp.Close(); err == nil {
				err = cerr
			}
			if err != nil {
				return "", nil, fmt.Errorf("storing upload failed: %v", err)
			}
		}
		part.Close()
	}
	if meta == nil || spool == "" {
		return "", nil, fmt.Errorf("incomplete form data")
	}

	backupName := meta.BackupName
	if backupName == "" {
		backupName = "Received_" + fsTimestamp()
	}

	var backupRoot string
	if rv.opts.flat {
		backupRoot = filepath.Join(rv.opts.outDir, sanitizeComponent(backupName))
	} else {
		dataType := meta.DataType
		if dataType != "extdata" {
			dataType = "save"
		}
		typeDir := map[string]string{"save": "saves", "extdata": "extdata"}[dataType]
		titleName := meta.TitleName
		if titleName == "" {
			titleName = "Unknown"
		}
		titleFolder := titleName
		if meta.TitleID != "" {
			titleFolder = meta.TitleID + " " + titleName
		}
		backupRoot = filepath.Join(rv.opts.outDir, typeDir, sanitizeComponent(titleFolder), sanitizeComponent(backupName))
	}

	// A pre-existing backup of the same name is replaced, like on console.
	if _, err := os.Stat(backupRoot); err == nil {
		if err := os.RemoveAll(backupRoot); err != nil {
			return "", nil, fmt.Errorf("cannot replace existing backup: %v", err)
		}
	}
	if err := os.MkdirAll(backupRoot, 0o755); err != nil {
		return "", nil, err
	}

	if rv.opts.verbose {
		fmt.Fprintf(os.Stderr, "received %s (%s, isZip=%v) -> %s\n", meta.FileName, humanBytes(spoolBytes), meta.IsZip, backupRoot)
	}

	if !meta.IsZip {
		fileName := sanitizeFileName(meta.FileName)
		if fileName == "" {
			fileName = "received.bin"
		}
		dst := filepath.Join(backupRoot, fileName)
		if err := os.Rename(spool, dst); err != nil {
			os.RemoveAll(backupRoot)
			return "", nil, err
		}
		spool = ""
		return backupRoot, meta, nil
	}

	if rv.opts.noExtract {
		dst := filepath.Join(backupRoot, sanitizeComponent(backupName)+".zip")
		if err := os.Rename(spool, dst); err != nil {
			os.RemoveAll(backupRoot)
			return "", nil, err
		}
		spool = ""
		return backupRoot, meta, nil
	}

	if err := ExtractZip(spool, backupRoot, rv.opts.verbose); err != nil {
		// Never keep a half-extracted backup.
		os.RemoveAll(backupRoot)
		return "", nil, fmt.Errorf("extract failed: %v", err)
	}
	if rv.opts.keepZip {
		dst := backupRoot + ".zip"
		if err := os.Rename(spool, dst); err == nil {
			spool = ""
		}
	}
	return backupRoot, meta, nil
}

func (rv *receiver) logEvent(fields map[string]any) {
	if rv.opts.jsonOut {
		json.NewEncoder(os.Stdout).Encode(fields)
		return
	}
	switch fields["event"] {
	case "received":
		fmt.Printf("received backup %q (%s / %s) -> %s\n",
			fields["backupName"], fields["titleName"], fields["dataType"], fields["savedPath"])
	case "upload_failed":
		fmt.Fprintf(os.Stderr, "upload failed: %v\n", fields["error"])
	}
}

func drainBody(r *http.Request) {
	io.Copy(io.Discard, r.Body)
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Connection", "close")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(v)
}

func localIPv4s() []string {
	var out []string
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return out
	}
	for _, a := range addrs {
		ipnet, ok := a.(*net.IPNet)
		if !ok || ipnet.IP.IsLoopback() {
			continue
		}
		if ip4 := ipnet.IP.To4(); ip4 != nil {
			out = append(out, ip4.String())
		}
	}
	if len(out) == 0 {
		out = append(out, "127.0.0.1")
	}
	return out
}
