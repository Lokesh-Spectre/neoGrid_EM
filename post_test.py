import json
from http.server import HTTPServer, BaseHTTPRequestHandler

HOST = "0.0.0.0"
PORT = 8000

class SimpleServer(BaseHTTPRequestHandler):
    """
    A simple HTTP request handler that pretty-prints JSON POST requests.
    """
    def do_POST(self):
        """Handle POST requests."""
        content_length = int(self.headers['Content-Length'])
        post_data_bytes = self.rfile.read(content_length)
        post_data_str = post_data_bytes.decode("utf-8")

        print("\n----- POST DATA RECEIVED -----")
        try:
            # Try to parse the data as JSON
            json_data = json.loads(post_data_str)
            # Pretty-print JSON with indentation
            pretty_json = json.dumps(json_data, indent=4)
            print(pretty_json)
        except json.JSONDecodeError:
            # If not JSON, print raw data
            print(post_data_str)
        print("----------------------------\n")

        self.send_response(200)
        self.send_header("Content-type", "text/plain")
        self.end_headers()
        self.wfile.write(b"POST request successfully received!")

    def do_GET(self):
        """Handle GET requests with a simple message."""
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(b"<html><body><h1>Server is running!</h1><p>Send a POST request to this endpoint to see it in the console.</p></body></html>")

if __name__ == "__main__":
    web_server = HTTPServer((HOST, PORT), SimpleServer)
    print(f"Server started at http://{HOST}:{PORT}")
    print("Press Ctrl+C to stop the server.")

    try:
        web_server.serve_forever()
    except KeyboardInterrupt:
        pass

    web_server.server_close()
    print("\nServer stopped.")
