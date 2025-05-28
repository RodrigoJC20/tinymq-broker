#!/bin/bash
set -e

DB_NAME="tinymq"
DB_USER="tinymq_broker"
DB_PASS="tinymq_password"
SCHEMA_PATH="./db/schema.sql"  # Path to your SQL schema file

echo "Dropping database (if exists)..."
psql -d postgres -c "DROP DATABASE IF EXISTS $DB_NAME;"

echo "Revoking privileges and cleaning user ownerships..."
psql -d postgres -c "REVOKE ALL PRIVILEGES ON SCHEMA public FROM $DB_USER;" || true
psql -d postgres -c "REASSIGN OWNED BY $DB_USER TO CURRENT_USER;" || true
psql -d postgres -c "DROP OWNED BY $DB_USER;" || true

echo "Dropping user (if exists)..."
psql -d postgres -c "DROP USER IF EXISTS $DB_USER;"

echo "Creating DB and user..."
psql -d postgres -c "CREATE DATABASE $DB_NAME;"
psql -d postgres -c "CREATE USER $DB_USER WITH PASSWORD '$DB_PASS';"
psql -d postgres -c "GRANT ALL PRIVILEGES ON DATABASE $DB_NAME TO $DB_USER;"

echo "Granting privileges on schema..."
psql -d $DB_NAME -c "GRANT ALL PRIVILEGES ON SCHEMA public TO $DB_USER;"

echo "Applying schema as user $DB_USER..."
export PGPASSWORD=$DB_PASS
psql -U $DB_USER -d $DB_NAME -h localhost -f "$SCHEMA_PATH"
unset PGPASSWORD

echo "Granting explicit table and sequence privileges to $DB_USER..."
export PGPASSWORD=$DB_PASS
psql -U $DB_USER -d $DB_NAME -h localhost -c "GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO $DB_USER;"
psql -U $DB_USER -d $DB_NAME -h localhost -c "GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO $DB_USER;"

echo "Setting default privileges for future objects for $DB_USER..."
psql -U $DB_USER -d $DB_NAME -h localhost -c "ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO $DB_USER;"
psql -U $DB_USER -d $DB_NAME -h localhost -c "ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT USAGE, SELECT ON SEQUENCES TO $DB_USER;"
unset PGPASSWORD

echo "✅ Database is ready."
