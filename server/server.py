from http.server import BaseHTTPRequestHandler, HTTPServer

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        print("\n===== GET REQUEST =====")
        print("Path:", self.path)
        print("Headers:")
        print(self.headers)

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"code":0,"msg":"hello from pc"}')

    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length)

        print("\n===== POST REQUEST =====")
        print("Path:", self.path)
        print("Headers:")
        print(self.headers)
        print("Body:", body.decode(errors="ignore"))

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"code":0,"msg":"post ok"}')

server = HTTPServer(("0.0.0.0", 8080), Handler)
print("Server running at port 8080...")
server.serve_forever()