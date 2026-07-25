package main

import (
	"flag"
	"fmt"
	"net"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// commonFlags are shared by every network-facing command.
type commonFlags struct {
	port    int
	verbose bool
	jsonOut bool
	timeout time.Duration
}

func addCommon(fs *flag.FlagSet) *commonFlags {
	c := &commonFlags{}
	fs.IntVar(&c.port, "port", defaultPort, "TCP port (both roles)")
	fs.BoolVar(&c.verbose, "verbose", false, "verbose output")
	fs.BoolVar(&c.jsonOut, "json", false, "machine-readable JSON output")
	fs.DurationVar(&c.timeout, "timeout", 30*time.Second, "connect/response timeout")
	return c
}

// parseOnePositional parses fs while allowing the single positional argument
// to come before the flags (Go's flag package stops parsing at the first
// non-flag argument, but the documented syntax is "chlink send <path> --to ...").
func parseOnePositional(fs *flag.FlagSet, args []string, what string) (string, error) {
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		if err := fs.Parse(args[1:]); err != nil {
			return "", err
		}
		if fs.NArg() != 0 {
			return "", fmt.Errorf("unexpected extra arguments: %v", fs.Args())
		}
		return args[0], nil
	}
	if err := fs.Parse(args); err != nil {
		return "", err
	}
	if fs.NArg() != 1 {
		fs.Usage()
		return "", fmt.Errorf("expected exactly one %s", what)
	}
	return fs.Arg(0), nil
}

// resolveTarget turns "ip" or "ip:port" into "host:port", defaulting the port.
func resolveTarget(to string, defaultPort int) (string, error) {
	if to == "" {
		return "", fmt.Errorf("missing --to <ip[:port]>")
	}
	host, port, err := net.SplitHostPort(to)
	if err != nil {
		// No port given.
		return net.JoinHostPort(to, strconv.Itoa(defaultPort)), nil
	}
	p, err := strconv.Atoi(port)
	if err != nil || p <= 0 || p > 65535 {
		return "", fmt.Errorf("invalid port in %q", to)
	}
	return net.JoinHostPort(host, port), nil
}

// validPin mirrors Transfer::validPin on the console: exactly 4 ASCII digits.
func validPin(pin string) bool {
	if len(pin) != 4 {
		return false
	}
	for _, c := range pin {
		if c < '0' || c > '9' {
			return false
		}
	}
	return true
}

// sanitizeComponent must match the console's removeForbiddenCharacters exactly,
// or the same title lands in a differently-named folder depending on which side
// stored the backup, breaking same-name replacement and path inference. The
// console (common/common.cpp) replaces each of these chars with a space and then
// trims trailing spaces — note it also strips '.', ',' and '!'.
func sanitizeComponent(s string) string {
	const illegal = ".,!\\/:?*\"<>|"
	var b strings.Builder
	for _, r := range s {
		if strings.ContainsRune(illegal, r) {
			b.WriteRune(' ')
		} else {
			b.WriteRune(r)
		}
	}
	out := strings.TrimRight(b.String(), " ")
	if out == "" {
		out = "_"
	}
	return out
}

// sanitizeFileName mirrors TransferProto::sanitizeFileName: it cleans the
// announced name of a single-file payload without touching the extension.
// sanitizeComponent must not be used here — it blanks '.' out, so a received
// "00000001.sav" would land as "00000001 sav" and no console could restore it.
// Returns "" when nothing usable is left.
func sanitizeFileName(name string) string {
	// Split on both separators regardless of host OS: the name comes off the
	// wire, so a Windows-style path must not survive on Linux either.
	base := name
	if i := strings.LastIndexAny(base, `/\`); i >= 0 {
		base = base[i+1:]
	}
	base = filepath.Base(base)
	var b strings.Builder
	for _, r := range base {
		if r < 0x20 || strings.ContainsRune(":*?\"<>|", r) {
			continue
		}
		b.WriteRune(r)
	}
	out := b.String()
	if out == "." || out == ".." {
		return ""
	}
	return out
}

func timestamp() string {
	return time.Now().Format("2006-01-02 15:04:05")
}

// fsTimestamp is a filesystem-safe variant used in default backup names.
func fsTimestamp() string {
	return time.Now().Format("20060102-150405")
}
