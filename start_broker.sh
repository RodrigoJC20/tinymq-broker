#!/bin/bash

# TinyMQ Broker startup script with database support

# Default configuration
PORT=1505
THREADS=4
DB_HOST="localhost"
DB_NAME="tinymq"
DB_USER="tinymq_broker"
DB_PASSWORD="tinymq_password"
USE_DATABASE=true

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --port)
      PORT="$2"
      shift 2
      ;;
    --threads)
      THREADS="$2"
      shift 2
      ;;
    --db-host)
      DB_HOST="$2"
      shift 2
      ;;
    --db-name)
      DB_NAME="$2"
      shift 2
      ;;
    --db-user)
      DB_USER="$2"
      shift 2
      ;;
    --db-password)
      DB_PASSWORD="$2"
      shift 2
      ;;
    --no-database)
      USE_DATABASE=false
      shift
      ;;
    --help)
      echo "TinyMQ Broker startup script"
      echo "Usage: $0 [options]"
      echo "Options:"
      echo "  --port PORT         Set the port number (default: 1505)"
      echo "  --threads N         Set thread pool size (default: 4)"
      echo "  --db-host HOST      PostgreSQL host (default: localhost)"
      echo "  --db-name NAME      PostgreSQL database name (default: tinymq)"
      echo "  --db-user USER      PostgreSQL user (default: tinymq_broker)"
      echo "  --db-password PASS  PostgreSQL password (default: tinymq_password)"
      echo "  --no-database       Run without database support"
      echo "  --help              Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use --help for usage information"
      exit 1
      ;;
  esac
done

# Find the executable (try build directory first, then current directory)
if [ -f "build/tinymq_broker" ]; then
  BROKER_EXECUTABLE="build/tinymq_broker"
elif [ -f "./tinymq_broker" ]; then
  BROKER_EXECUTABLE="./tinymq_broker"
else
  echo "Error: Could not find tinymq_broker executable."
  echo "Make sure you've built the project and are running this script from the project directory."
  exit 1
fi

# Construct command line
CMD="$BROKER_EXECUTABLE --port $PORT --threads $THREADS"

# Add database connection if enabled
if [ "$USE_DATABASE" = true ]; then
  DB_CONNECTION="dbname=$DB_NAME user=$DB_USER password=$DB_PASSWORD host=$DB_HOST"
  CMD="$CMD --db \"$DB_CONNECTION\""
  echo "Starting TinyMQ Broker with database support..."
else
  echo "Starting TinyMQ Broker without database support..."
fi

# Print and execute the command
echo "Command: $CMD"
eval $CMD 