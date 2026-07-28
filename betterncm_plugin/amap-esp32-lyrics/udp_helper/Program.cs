using System.Collections.Concurrent;
using System.Drawing;
using System.Drawing.Imaging;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

const int MaxRequestBytes = 4096;
var listenPort = 25835;
for (var index = 0; index + 1 < args.Length; index++) {
    if (args[index] == "--listen" && int.TryParse(args[index + 1], out var requestedPort) && requestedPort is > 0 and <= 65535) listenPort = requestedPort;
}

// The helper accepts BetterNCM only on loopback, but the cover endpoint must be
// reachable by the ESP32. URLs are unguessable cache keys, not arbitrary fetches.
var listener = new TcpListener(IPAddress.Any, listenPort);
try { listener.Start(); }
catch (SocketException) { return; }
using var udpBridge = new UdpBridge();

while (true) {
    var client = await listener.AcceptTcpClientAsync();
    _ = Task.Run(async () => {
        using (client) {
            NetworkStream? stream = null;
            try {
                client.ReceiveTimeout = 4000;
                stream = client.GetStream();
                var request = await ReadRequest(stream);
                if (request is null) return;
                if (request.Method.Equals("OPTIONS", StringComparison.OrdinalIgnoreCase)) { await Respond(stream, 204, ""); return; }
                if (request.Method.Equals("GET", StringComparison.OrdinalIgnoreCase) && request.Path.StartsWith("/cover/", StringComparison.OrdinalIgnoreCase)) {
                    await ServeCover(stream, request.Path[7..]); return;
                }
                if (!request.Method.Equals("POST", StringComparison.OrdinalIgnoreCase) || !request.Path.Equals("/v1/send", StringComparison.OrdinalIgnoreCase) || request.Body.Length is <= 0 or > MaxRequestBytes) {
                    await Respond(stream, 400, "{\"ok\":false,\"error\":\"invalid request\"}"); return;
                }
                var values = ParseQuery(request.Query);
                if (!values.TryGetValue("host", out var host) || !IPAddress.TryParse(host, out var destination) || !values.TryGetValue("port", out var portText) || !int.TryParse(portText, out var port) || port is < 1 or > 65535) {
                    await Respond(stream, 400, "{\"ok\":false,\"error\":\"invalid destination\"}"); return;
                }
                var localIp = UdpBridge.RouteLocalAddress(destination, port);
                var payload = CoverProxy.RewritePayload(request.Body, localIp, listenPort);
                await udpBridge.SendAsync(payload, destination, port);
                var response = new JsonObject {
                    ["ok"] = true,
                    ["transport"] = "udp",
                    ["proto"] = 1,
                    ["controls"] = JsonSerializer.SerializeToNode(udpBridge.DrainControls()),
                };
                await Respond(stream, 200, response.ToJsonString());
            } catch {
                try { if (stream is not null) await Respond(stream, 500, "{\"ok\":false,\"error\":\"udp helper failure\"}"); } catch { }
            }
        }
    });
}

static async Task ServeCover(NetworkStream stream, string key) {
    try {
        var jpeg = await CoverProxy.GetBaselineJpeg(key);
        if (jpeg is null) { await Respond(stream, 404, "{\"ok\":false}"); return; }
        await RespondBytes(stream, 200, "image/jpeg", jpeg);
    } catch { await Respond(stream, 500, "{\"ok\":false}"); }
}

static async Task<HttpRequest?> ReadRequest(NetworkStream stream) {
    var bytes = new byte[MaxRequestBytes + 2048];
    var total = 0; var headerEnd = -1;
    while (headerEnd < 0 && total < bytes.Length) {
        var count = await stream.ReadAsync(bytes.AsMemory(total, bytes.Length - total));
        if (count == 0) return null;
        total += count; headerEnd = FindHeaderEnd(bytes, total);
    }
    if (headerEnd < 0) return null;
    var header = Encoding.ASCII.GetString(bytes, 0, headerEnd);
    var lines = header.Split("\r\n", StringSplitOptions.None);
    var parts = lines[0].Split(' ', StringSplitOptions.RemoveEmptyEntries);
    if (parts.Length < 2) return null;
    var contentLength = 0;
    foreach (var line in lines.Skip(1)) { var separator = line.IndexOf(':'); if (separator > 0 && line[..separator].Equals("Content-Length", StringComparison.OrdinalIgnoreCase)) int.TryParse(line[(separator + 1)..].Trim(), out contentLength); }
    if (contentLength is < 0 or > MaxRequestBytes) return null;
    var bodyStart = headerEnd + 4;
    while (total - bodyStart < contentLength) {
        if (total >= bytes.Length) return null;
        var count = await stream.ReadAsync(bytes.AsMemory(total, bytes.Length - total));
        if (count == 0) return null;
        total += count;
    }
    var target = parts[1]; var queryAt = target.IndexOf('?');
    return new HttpRequest(parts[0], queryAt < 0 ? target : target[..queryAt], queryAt < 0 ? "" : target[(queryAt + 1)..], bytes[bodyStart..(bodyStart + contentLength)]);
}

static int FindHeaderEnd(byte[] bytes, int count) { for (var index = 3; index < count; index++) if (bytes[index - 3] == '\r' && bytes[index - 2] == '\n' && bytes[index - 1] == '\r' && bytes[index] == '\n') return index - 3; return -1; }
static Dictionary<string, string> ParseQuery(string query) => query.Split('&', StringSplitOptions.RemoveEmptyEntries).Select(item => item.Split('=', 2)).Where(pair => pair.Length == 2).ToDictionary(pair => Uri.UnescapeDataString(pair[0]), pair => Uri.UnescapeDataString(pair[1]), StringComparer.OrdinalIgnoreCase);
static async Task Respond(NetworkStream stream, int status, string body) => await RespondBytes(stream, status, "application/json; charset=utf-8", Encoding.UTF8.GetBytes(body));
static async Task RespondBytes(NetworkStream stream, int status, string type, byte[] bytes) {
    var statusText = status == 200 ? "OK" : status == 204 ? "No Content" : status == 404 ? "Not Found" : "Bad Request";
    var headers = $"HTTP/1.1 {status} {statusText}\r\nContent-Type: {type}\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nContent-Length: {bytes.Length}\r\nConnection: close\r\n\r\n";
    await stream.WriteAsync(Encoding.ASCII.GetBytes(headers)); if (bytes.Length > 0) await stream.WriteAsync(bytes);
}
sealed record HttpRequest(string Method, string Path, string Query, byte[] Body);

sealed class UdpBridge : IDisposable {
    private readonly UdpClient client = new(new IPEndPoint(IPAddress.Any, 0));
    private readonly ConcurrentQueue<string> controls = new();

    public UdpBridge() { _ = Task.Run(ReceiveLoop); }

    public async Task SendAsync(byte[] payload, IPAddress destination, int port) {
        await client.SendAsync(payload, payload.Length, new IPEndPoint(destination, port));
    }

    public string[] DrainControls() {
        var drained = new List<string>();
        while (controls.TryDequeue(out var action)) drained.Add(action);
        return drained.ToArray();
    }

    public static IPAddress RouteLocalAddress(IPAddress destination, int port) {
        using var route = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
        route.Connect(new IPEndPoint(destination, port));
        return ((IPEndPoint)route.LocalEndPoint!).Address;
    }

    private async Task ReceiveLoop() {
        while (true) {
            try {
                var packet = await client.ReceiveAsync();
                var root = JsonNode.Parse(packet.Buffer)?.AsObject();
                if (root?["type"]?.GetValue<string>() != "media_control") continue;
                var action = root["action"]?.GetValue<string>();
                if (action is "previous" or "play_pause" or "next") controls.Enqueue(action);
            } catch (ObjectDisposedException) { return; }
            catch (SocketException) { return; }
            catch { }
        }
    }

    public void Dispose() { client.Dispose(); }
}

static class CoverProxy {
    private static readonly ConcurrentDictionary<string, string> Sources = new();
    private static readonly HttpClient Client = new() { Timeout = TimeSpan.FromSeconds(12) };

    public static byte[] RewritePayload(byte[] payload, IPAddress localIp, int port) {
        var root = JsonNode.Parse(payload)?.AsObject();
        var music = root?["music"]?.AsObject();
        var source = music?["coverUrl"]?.GetValue<string>();
        if (string.IsNullOrWhiteSpace(source)) return payload;
        var key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(source)))[..24].ToLowerInvariant();
        Sources[key] = source;
        music!["coverUrl"] = $"http://{localIp}:{port}/cover/{key}.jpg";
        return Encoding.UTF8.GetBytes(root!.ToJsonString());
    }

    public static async Task<byte[]?> GetBaselineJpeg(string filename) {
        var key = filename.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase) ? filename[..^4] : filename;
        if (!Sources.TryGetValue(key, out var source)) return null;
        var input = await Client.GetByteArrayAsync(source + (source.Contains('?') ? "&param=128y128" : "?param=128y128"));
        using var sourceStream = new MemoryStream(input);
        using var image = Image.FromStream(sourceStream);
        using var bitmap = new Bitmap(128, 128);
        using (var graphics = Graphics.FromImage(bitmap)) {
            graphics.Clear(Color.Black);
            graphics.DrawImage(image, new Rectangle(0, 0, 128, 128));
        }
        using var output = new MemoryStream();
        bitmap.Save(output, ImageFormat.Jpeg);
        return output.ToArray();
    }
}
