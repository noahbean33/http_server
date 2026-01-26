# HTTP/1.1 Server in C

A lightweight, concurrent HTTP/1.1 server implementation written in C using POSIX threads. This server handles multiple simultaneous client connections and supports various HTTP endpoints including file operations.

## Features

- **Concurrent Connections** - Uses pthreads to handle multiple clients simultaneously
- **HTTP/1.1 Protocol** - Compliant with HTTP/1.1 specifications
- **Multiple Endpoints** - Supports routing to different URL paths
- **File Operations** - GET and POST methods for file serving and creation
- **Header Parsing** - Extracts and processes HTTP headers
- **Binary Safe** - Properly handles binary file content

## Supported Endpoints

### `GET /`
Returns a simple 200 OK response.

**Example:**
```bash
curl http://localhost:4221/
# HTTP/1.1 200 OK
```

### `GET /echo/{string}`
Echoes back the string provided in the URL path.

**Example:**
```bash
curl http://localhost:4221/echo/hello
# HTTP/1.1 200 OK
# Content-Type: text/plain
# Content-Length: 5
#
# hello
```

### `GET /user-agent`
Returns the User-Agent header from the client's request.

**Example:**
```bash
curl http://localhost:4221/user-agent
# HTTP/1.1 200 OK
# Content-Type: text/plain
# Content-Length: 11
#
# curl/7.64.1
```

### `GET /files/{filename}` (requires --directory flag)
Retrieves a file from the specified directory.

**Example:**
```bash
./your_program.sh --directory /tmp/
curl http://localhost:4221/files/example.txt
# HTTP/1.1 200 OK
# Content-Type: application/octet-stream
# Content-Length: <file_size>
#
# <file contents>
```

### `POST /files/{filename}` (requires --directory flag)
Creates a new file with the request body as content.

**Example:**
```bash
./your_program.sh --directory /tmp/
curl -X POST --data "Hello, World!" \
  -H "Content-Type: application/octet-stream" \
  http://localhost:4221/files/newfile.txt
# HTTP/1.1 201 Created
```

## Building and Running

### Prerequisites
- GCC or compatible C compiler
- POSIX-compliant operating system (Linux, macOS, etc.)
- pthread library

### Compilation
```bash
gcc -o server src/main.c -lpthread
```

Or use the provided script:
```bash
./your_program.sh
```

### Usage
```bash
# Basic usage (no file operations)
./server

# With file operations enabled
./server --directory /path/to/files/
```

### Testing
```bash
# Test the root endpoint
curl http://localhost:4221/

# Test echo endpoint
curl http://localhost:4221/echo/test

# Test user-agent endpoint
curl http://localhost:4221/user-agent

# Test file operations (with --directory flag)
echo "test content" > /tmp/test.txt
./server --directory /tmp/
curl http://localhost:4221/files/test.txt
```

## Architecture

### Threading Model
The server uses a **thread-per-connection** model:
1. Main thread listens for incoming connections on port 4221
2. Each accepted connection spawns a new pthread
3. The thread handles the complete request-response cycle
4. Thread is detached for automatic cleanup after completion

### Request Processing Flow
1. **Accept Connection** - Main thread accepts TCP connection
2. **Create Thread** - Spawns new thread with client socket and directory info
3. **Read Request** - Thread reads HTTP request from socket
4. **Parse Request** - Extracts HTTP method, path, and headers
5. **Route Request** - Matches path to appropriate endpoint handler
6. **Generate Response** - Creates HTTP response with proper status and headers
7. **Send Response** - Writes response to client socket
8. **Close Connection** - Closes socket and thread exits

## Code Structure

```
src/main.c
├── extract_header_value()  - Parses HTTP headers
├── handle_client()         - Thread function for client handling
└── main()                  - Server initialization and main loop
```

### Key Components

**`client_data_t`** - Structure passed to each thread containing:
- `client_fd` - Socket file descriptor for the client
- `directory` - Optional directory path for file operations

**`extract_header_value()`** - Helper function to extract specific header values from HTTP requests

**`handle_client()`** - Main request handler that:
- Parses HTTP request line and headers
- Routes to appropriate endpoint
- Generates and sends HTTP response
- Handles errors and edge cases

**`main()`** - Server setup:
- Parses command-line arguments
- Creates and configures TCP socket
- Listens on port 4221
- Accepts connections and spawns handler threads

## Technical Details

- **Port:** 4221
- **Protocol:** HTTP/1.1
- **Concurrency:** POSIX threads (pthread)
- **Socket Options:** SO_REUSEADDR enabled
- **Connection Backlog:** 5 pending connections
- **Buffer Size:** 1024 bytes for requests

## HTTP Response Codes

- `200 OK` - Successful request
- `201 Created` - File successfully created (POST)
- `400 Bad Request` - Missing required headers or invalid request
- `404 Not Found` - Resource not found
- `500 Internal Server Error` - Server-side error (e.g., file creation failed)
