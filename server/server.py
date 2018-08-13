#!/usr/bin/env python3

from flask import Flask, jsonify, json
import mysql.connector
import os
import atexit

# DEFINITIONS

app = Flask(__name__)
connection_params_json = "connection.json"
dbname_default = "checkpoint"
dao = None

# SERVICES

@app.route('/')
def index():
    return jsonify({"message": "Checkpoint server running..."})

# UTILS

def exit_handler():
    print("Closing database connection...")
    dao.close()

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
            id bigint,
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
        print(e)
        print("Couldn't setup database. Closing...")
        exit()
    finally:
        connection.close()

if __name__ == '__main__':
    # load connection parameters
    connection_json = json.load(open(connection_params_json, "r"))
    hostname = connection_json["hostname"]
    username = connection_json["username"]
    password = connection_json["password"]
    dbname = None

    if not username or not password or not hostname:
        print("ERROR: Please setup username, password and hostname in connection.json.")
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
    app.run(debug=True)