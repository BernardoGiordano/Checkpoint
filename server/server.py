#!/usr/bin/env python3
from flask import Flask, request, jsonify
from flask.logging import default_handler
from logging.config import dictConfig
import atexit
import json
import hashlib
import mysql.connector
import os


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
connection_params_json = "connection.json"
dbname_default = "checkpoint"
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
        return createAndLogResult("INTERNAL_ERROR")
    if result is 0:
        return createAndLogResult("NOT_FOUND")
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
    return update("""
        DELETE FROM SAVES
        WHERE id={}
        AND serial='{}'
    """.format(id, serial))


# Utils
def sha256(value):
    return hashlib.sha256(value.encode('utf-8')).hexdigest()

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
    value = switcher.get(argument)
    if value is None:
        return createResult("GENERIC_ERROR")
    else:
        return value

def createAndLogResult(argument):
    result = createResult(argument)
    info(result)
    return jsonify(result)

def info(result):
    app.logger.info("Served with result %d: %s", result['code'], result['message'])

def createDatabase(hostname, username, password):
    connection = mysql.connector.connect(host=hostname, user=username, passwd=password, auth_plugin="mysql_native_password")
    try:
        connection.cursor().execute("DROP DATABASE {}".format(dbname_default))
    except mysql.connector.errors.DatabaseError as e:
        print("Assuring no databases are named {}...".format(dbname_default))
    connection.cursor().execute("CREATE DATABASE {}".format(dbname_default))
    connection.close()
    
    connection = mysql.connector.connect(host=hostname, user=username, passwd=password, db=dbname_default, auth_plugin="mysql_native_password")
    try:
        connection.cursor().execute("""
            CREATE TABLE saves (
            id bigint AUTO_INCREMENT,
            date datetime,
            hash char(32) NOT NULL,
            path varchar(255) NOT NULL,
            private boolean,
            product_code varchar(32),
            serial char(64) NOT NULL,
            title_id bigint,
            type enum('3DS', 'Switch') NOT NULL,
            username varchar(255),
            PRIMARY KEY (id))
        """)
    except mysql.connector.errors.ProgrammingError as e:
        print("Couldn't setup database. Closing...")
        exit()
    finally:
        connection.close()

if __name__ == '__main__':
    # load connection parameters
    connection_json = json.load(open(connection_params_json, "r"))
    hostname = connection_json["hostname"]
    port = connection_json["port"]
    username = connection_json["username"]
    password = connection_json["password"]
    dbname = None

    if not username or not password or not hostname or not port:
        print("ERROR: Please setup username, password, hostname and port in connection.json.")
        exit()
    try:
        dbname = connection_json["dbname"]
    except KeyError as e:
        print("Database name not found, creating a new database...")
        createDatabase(hostname, username, password)
        connection_json["dbname"] = dbname = dbname_default
        with open(connection_params_json, "w") as f:
            f.write(json.dumps(connection_json, indent=2))
    
    # connect to mysql database
    print("Opening database connection...")
    try:
        dao = mysql.connector.connect(host=hostname, user=username, passwd=password, db=dbname, auth_plugin="mysql_native_password")
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
        debug=False
    )