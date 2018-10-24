#!/usr/bin/env python3
from flask import Flask, request, jsonify
from flask.logging import default_handler
from logging.config import dictConfig
from datetime import datetime
import argparse
import atexit
import json
import hashlib
import mysql.connector
import os
import traceback

parser = argparse.ArgumentParser(description = "Checkpoint server")
parser.add_argument("-hostname", help = "Hostname of the machine running the database", default = "localhost")
parser.add_argument("-username", help = "Database username")
parser.add_argument("-password", help = "Database password")
parser.add_argument("-port", help = "Database port", default = 5000)

# Couple variables and constants
dictConfig({
    'version': 1,
    'formatters': {'default': {
        'format': '[%(asctime)s] %(levelname)s in %(module)s: %(message)s',
    }},
    'handlers': {'wsgi': {
        'class': 'logging.StreamHandler',
        'stream': 'ext://flask.logging.wsgi_errors_stream',
        'formatter': 'default'
    }},
    'root': {
        'level': 'INFO',
        'handlers': ['wsgi']
    }
})
app = Flask(__name__)
dbname_default = "checkpoint"
directory_default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "saves")
dao = None

# Services
@app.route('/')
def index():
    return createAndLogResult("RUNNING")

@app.route('/api/v1/saves/<int:titleId>', methods=['GET'])
def savesById(titleId):
    app.logger.info("Resource savesById called for Title Id: %d", titleId)
    try:
        serial = request.headers.get('Serial')
        if not serial or not titleId:
            return createAndLogResult("INVALID_PARAMETERS")
        result = getSavesByTitleId(titleId, sha256(serial))
    except Exception as e:
        trace(e)
        return createAndLogResult("INTERNAL_ERROR")
    if result is None:
        return createAndLogResult("NOT_FOUND")
    else:
        createAndLogResult("OK_CONTENT")
        return jsonify(result)

@app.route('/api/v1/save/<int:id>', methods=['GET'])
def saveById(id):
    app.logger.info("Resource saveById called for Id: %d", id)
    try:
        serial = request.headers.get('Serial')
        if not serial or not id:
            return createAndLogResult("INVALID_PARAMETERS")
        result = getSaveById(id, sha256(serial))
    except Exception as e:
        trace(e)
        return createAndLogResult("INTERNAL_ERROR")
    if result is None:
        return createAndLogResult("NOT_FOUND")
    else:
        createAndLogResult("OK_CONTENT")
        return jsonify(result)

@app.route('/api/v1/save/<int:id>', methods=['DELETE'])
def deleteById(id):
    app.logger.info("Resource deleteById called for Id: %d", id)
    try:
        serial = request.headers.get('Serial')
        if not serial or not id:
            return createAndLogResult("INVALID_PARAMETERS")
        result = deleteSaveById(id, sha256(serial))
    except Exception as e:
        trace(e)
        return createAndLogResult("INTERNAL_ERROR")
    if result is 0:
        return createAndLogResult("NOT_FOUND")
    elif result is 1:
        return createAndLogResult("OK_NO_CONTENT")
    else:
        return createAndLogResult("")

@app.route('/api/v1/save', methods=['PUT'])
def putSave():
    app.logger.info("Resource putSave called")
    try:
        serial = request.headers.get('Serial')
        metadata = json.loads(request.files.get('metadata').read())      
        attachment = request.files.get('attachment')
        if not serial or not metadata or not attachment:
            return createAndLogResult("INVALID_PARAMETERS")
        if metadata["hash"] and len(metadata["hash"]) == 32 \
            and metadata["name"] \
            and metadata["serial"] and len(metadata["serial"]) == 64 \
            and not (metadata["title_id"] is None and (metadata["type"] is '3DS' or metadata["type"] is 'Switch')):
            with open(os.path.join(directory_default, "{}_{}".format(os.path.basename(os.path.normpath(metadata["name"])), actualTime())), "wb") as f:
                f.write(attachment.read())
            result = createSave(metadata)
            print(result)
            if result is 0:
                return createAndLogResult("INTERNAL_ERROR")
            elif result is 1:
                return createAndLogResult("OK_NO_CONTENT")
            else:
                return createAndLogResult("")
        else:
            return createAndLogResult("INVALID_PARAMETERS")
    except Exception as e:
        trace(e)
        return createAndLogResult("INTERNAL_ERROR")
    if result is 0:
        return createAndLogResult("INTERNAL_ERROR")
    elif result is 1:
        return createAndLogResult("OK_NO_CONTENT")
    else:
        return createAndLogResult("")

# Database methods
def queryForObject(query):
    cursor = dao.cursor(buffered=True)
    if cursor.execute(query) is 0:
        return None
    else:
        columns = [x[0] for x in cursor.description]
        result = cursor.fetchall()
        json_data = []
        for item in result:
            json_data.append(dict(zip(columns, item)))
        return json_data

def update(query):
    cursor = dao.cursor()
    cursor.execute(query)
    return cursor.rowcount

def getSavesByTitleId(titleId, serial):
    return queryForObject("""
        SELECT * FROM saves
        WHERE title_id={} 
        AND (serial='{}' OR private=false)
        """.format(titleId, serial))

def getSaveById(id, serial):
    return queryForObject("""
        SELECT * FROM saves
        WHERE id={}
        AND (serial='{}' OR private=false)
    """.format(id, serial))

def deleteSaveById(id, serial):
    query = """
        DELETE FROM SAVES
        WHERE id={}
        AND serial='{}'
    """.format(id, serial)
    print(query)
    return update(query)

def createSave(metadata):
    mhash = "'{}'".format(metadata["hash"])
    mname = "'{}'".format(os.path.basename(os.path.normpath(metadata["name"])))
    mprivate = "true" if "private" in metadata and metadata["private"] is True else "false"
    mproduct_code = "'{}'".format(metadata["product_code"]) if "product_code" in metadata else "NULL"
    mserial = "'{}'".format(metadata["serial"])
    mtitle_id = "'{}'".format(metadata["title_id"]) if "title_id" in metadata else "NULL"
    mtype = "'{}'".format(metadata["type"])
    musername = "'{}'".format(metadata["username"]) if "username" in metadata else "NULL"

    query = """
        INSERT INTO saves (
            date, hash,
            path, private,
            product_code, serial,
            title_id, type, username
        ) VALUES (now(),{},{},{},{},{},{},{},{})
    """.format(mhash, mname, mprivate, mproduct_code, mserial, mtitle_id, mtype, musername)
    print(query)
    return update(query)


# Utils
def trace(err):
    traceback.print_tb(err.__traceback__)

def sha256(value):
    return hashlib.sha256(value.encode('utf-8')).hexdigest()

def md5ForFile(f, block_size=2**20):
    md5 = hashlib.md5()
    while True:
        data = f.read(block_size)
        if not data:
            break
        md5.update(data)
    return md5.digest()

def exit_handler():
    print("Closing database connection...")
    dao.close()

def createResult(argument):
    switcher = {
        "RUNNING":            {'code': 200, 'message': 'Checkpoint server running...'},
        "OK_CONTENT":         {'code': 200, 'message': 'OK content'},
        "OK_NO_CONTENT":      {'code': 204, 'message': 'OK no content'},
        "BAD_REQUEST":        {'code': 400, 'message': 'Bad request'},
        "INVALID_PARAMETERS": {'code': 400, 'message': 'Invalid parameters'},
        "NOT_FOUND":          {'code': 404, 'message': 'Not found'},
        "INTERNAL_ERROR":     {'code': 500, 'message': 'Internal server error'},
        "GENERIC_ERROR":      {'code': 500, 'message': 'Generic server error'}
    }
    return switcher.get(argument, switcher["GENERIC_ERROR"])

def createAndLogResult(argument):
    result = createResult(argument)
    info(result)
    return jsonify(result)

def info(result):
    app.logger.info("Served with result %d: %s", result['code'], result['message'])

def actualTime():
    return str(datetime.now()).replace(":", "")

def createDatabase(hostname, username, password):
    connection = mysql.connector.connect(host=hostname, user=username, passwd=password, auth_plugin="mysql_native_password")
    try:
        connection.cursor().execute("CREATE DATABASE {}".format(dbname_default))
        connection.close()
        connection = mysql.connector.connect(host=hostname, user=username, passwd=password, db=dbname_default, auth_plugin="mysql_native_password")
        try:
            connection.cursor().execute("""
                CREATE TABLE saves (
                id bigint NOT NULL AUTO_INCREMENT,
                date datetime,
                hash char(32) NOT NULL,
                path varchar(255) NOT NULL,
                private boolean,
                product_code varchar(32),
                serial char(64) NOT NULL,
                title_id bigint,
                type enum('DS', '3DS', 'Switch') NOT NULL,
                username varchar(255),
                PRIMARY KEY (id))
            """)
            print("Database created.")
        except mysql.connector.errors.ProgrammingError as e:
            print("Couldn't setup database. Closing...")
            exit()
        finally:
            connection.close()
    except mysql.connector.errors.DatabaseError as e:
        print("Didn't create the database in this launch.")
    finally:
        connection.close()

if __name__ == '__main__':
    args = parser.parse_args()
    hostname = args.hostname
    username = args.username
    password = args.password
    port = args.port

    if not username or not password:
        exit()
    
    createDatabase(hostname, username, password)
    
    # connect to mysql database
    print("Opening database connection...")
    try:
        dao = mysql.connector.connect(host=hostname, user=username, passwd=password, db=dbname_default, auth_plugin="mysql_native_password")
    except mysql.connector.errors.ProgrammingError as e:
        print("Can't connect to database. Closing...")
        exit()

    # close connections and cleanup
    atexit.register(exit_handler)

    # serve
    app.logger.removeHandler(default_handler)
    app.run(
        host='0.0.0.0',
        port=port,
        debug=True
    )