#!/usr/bin/env python3
from flask import Flask, request, abort, jsonify
import json
import mysql.connector
import os
import atexit
import hashlib


# Couple variables and constants
app = Flask(__name__)
connection_params_json = "connection.json"
dbname_default = "checkpoint"
dao = None


# Services
@app.route('/')
def index():
    return createResult("RUNNING")

@app.route('/api/v1/saves/<int:titleId>', methods=['GET'])
def savesById(titleId):
    try:
        serial = request.headers.get('Serial')
        if not serial or not titleId:
            return createResult("INVALID_PARAMETERS")
        result = getSavesByTitleId(titleId, sha256(serial))
    except Exception as e:
        return createResult("INTERNAL_ERROR")
    if result is None:
        return createResult("NOT_FOUND")
    else:
        return jsonify(result)

@app.route('/api/v1/save/<int:id>', methods=['GET'])
def saveById(id):
    try:
        serial = request.headers.get('Serial')
        if not serial or not id:
            return createResult("INVALID_PARAMETERS")
        result = getSaveById(id, sha256(serial))
    except Exception as e:
        return createResult("INTERNAL_ERROR")
    if result is None:
        return createResult("NOT_FOUND")
    else:
        return jsonify(result)


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
    return jsonify(switcher.get(argument, lambda: {'code': 500, 'message': 'Generic server error'}))

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
    app.run(
        host='0.0.0.0',
        port=port,
        debug=True
    )