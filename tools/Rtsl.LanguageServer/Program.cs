using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;

namespace Rtsl.LanguageServer
{
    internal static class Program
    {
        private static readonly Dictionary<string, Document> Documents = new Dictionary<string, Document>(StringComparer.OrdinalIgnoreCase);
        private static readonly JavaScriptSerializer Json = new JavaScriptSerializer();

        private static void Main()
        {
            Stream input = Console.OpenStandardInput();
            Stream output = Console.OpenStandardOutput();
            while (true)
            {
                string payload = ReadMessage(input);
                if (payload == null)
                    return;

                Dictionary<string, object> request;
                try
                {
                    request = Json.DeserializeObject(payload) as Dictionary<string, object>;
                }
                catch
                {
                    continue;
                }
                if (request == null)
                    continue;

                string method = StringValue(request, "method");
                object id = request.ContainsKey("id") ? request["id"] : null;
                Dictionary<string, object> parameters = request.ContainsKey("params") ? request["params"] as Dictionary<string, object> : null;
                if (method == "initialize")
                {
                    Reply(output, id, new Dictionary<string, object>
                    {
                        ["capabilities"] = new Dictionary<string, object>
                        {
                            ["textDocumentSync"] = 1,
                            ["completionProvider"] = new Dictionary<string, object> { ["triggerCharacters"] = new[] { ".", ":", "(" } },
                            ["hoverProvider"] = true,
                            ["definitionProvider"] = true,
                            ["documentSymbolProvider"] = true,
                            ["signatureHelpProvider"] = new Dictionary<string, object> { ["triggerCharacters"] = new[] { "(" , "," } },
                            ["semanticTokensProvider"] = new Dictionary<string, object>
                            {
                                ["legend"] = new Dictionary<string, object>
                                {
                                    ["tokenTypes"] = new[] { "keyword", "string", "number", "comment", "operator", "variable", "type", "function", "property" },
                                    ["tokenModifiers"] = new string[0]
                                },
                                ["full"] = true
                            }
                        }
                    });
                    continue;
                }
                if (method == "initialized")
                    continue;
                if (method == "textDocument/didOpen" || method == "textDocument/didChange")
                {
                    UpdateDocument(parameters, method == "textDocument/didOpen");
                    string uri = UriValue(parameters);
                    if (uri != null && Documents.ContainsKey(uri))
                        Notify(output, "textDocument/publishDiagnostics", new Dictionary<string, object> { ["uri"] = uri, ["diagnostics"] = Documents[uri].Diagnostics() });
                    continue;
                }
                if (method == "textDocument/semanticTokens/full")
                {
                    Document document = GetDocument(parameters);
                    Reply(output, id, new Dictionary<string, object> { ["data"] = document == null ? new int[0] : document.SemanticTokens() });
                    continue;
                }
                if (method == "textDocument/completion")
                {
                    Document document = GetDocument(parameters);
                    Reply(output, id, document == null ? new Dictionary<string, object> { ["isIncomplete"] = false, ["items"] = new object[0] } : document.Completion(Position(parameters)));
                    continue;
                }
                if (method == "textDocument/hover")
                {
                    Document document = GetDocument(parameters);
                    Reply(output, id, document == null ? null : document.Hover(Position(parameters)));
                    continue;
                }
                if (method == "textDocument/definition")
                {
                    Document document = GetDocument(parameters);
                    Reply(output, id, document == null ? new object[0] : document.Definition(Position(parameters)));
                    continue;
                }
                if (method == "textDocument/documentSymbol")
                {
                    Document document = GetDocument(parameters);
                    Reply(output, id, document == null ? new object[0] : document.DocumentSymbols());
                    continue;
                }
                if (method == "textDocument/signatureHelp")
                {
                    Document document = GetDocument(parameters);
                    Reply(output, id, document == null ? null : document.SignatureHelp(Position(parameters)));
                    continue;
                }
                if (method == "shutdown")
                {
                    Reply(output, id, null);
                    continue;
                }
                if (method == "exit")
                    return;
                if (id != null)
                    Reply(output, id, null);
            }
        }

        private static void UpdateDocument(Dictionary<string, object> parameters, bool open)
        {
            string uri = UriValue(parameters);
            if (uri == null)
                return;
            string text = null;
            Dictionary<string, object> textDocument = parameters["textDocument"] as Dictionary<string, object>;
            if (open)
                text = StringValue(textDocument, "text");
            else
            {
                object[] changes = parameters["contentChanges"] as object[];
                if (changes != null && changes.Length != 0)
                    text = StringValue(changes[changes.Length - 1] as Dictionary<string, object>, "text");
            }
            if (text != null)
                Documents[uri] = new Document(uri, text);
        }

        private static Document GetDocument(Dictionary<string, object> parameters)
        {
            string uri = UriValue(parameters);
            Document document;
            return uri != null && Documents.TryGetValue(uri, out document) ? document : null;
        }

        private static string UriValue(Dictionary<string, object> parameters)
        {
            return parameters == null ? null : StringValue(parameters["textDocument"] as Dictionary<string, object>, "uri");
        }

        private static Position Position(Dictionary<string, object> parameters)
        {
            Dictionary<string, object> value = parameters == null ? null : parameters["position"] as Dictionary<string, object>;
            return new Position(NumberValue(value, "line"), NumberValue(value, "character"));
        }

        private static string StringValue(Dictionary<string, object> value, string key)
        {
            object result;
            return value != null && value.TryGetValue(key, out result) ? result as string : null;
        }

        private static int NumberValue(Dictionary<string, object> value, string key)
        {
            object result;
            if (value == null || !value.TryGetValue(key, out result))
                return 0;
            return Convert.ToInt32(result);
        }

        private static string ReadMessage(Stream input)
        {
            StringBuilder header = new StringBuilder();
            int previous = 0;
            int current;
            while ((current = input.ReadByte()) >= 0)
            {
                header.Append((char)current);
                if (previous == '\r' && current == '\n' && header.Length >= 4 && header[header.Length - 4] == '\r' && header[header.Length - 3] == '\n')
                    break;
                previous = current;
            }
            if (current < 0)
                return null;
            int length = 0;
            foreach (string line in header.ToString().Split(new[] { "\r\n" }, StringSplitOptions.RemoveEmptyEntries))
            {
                if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase))
                    length = int.Parse(line.Substring(15).Trim());
            }
            byte[] bytes = new byte[length];
            int offset = 0;
            while (offset < bytes.Length)
            {
                int read = input.Read(bytes, offset, bytes.Length - offset);
                if (read <= 0)
                    return null;
                offset += read;
            }
            return Encoding.UTF8.GetString(bytes);
        }

        private static void Reply(Stream output, object id, object result)
        {
            Send(output, new Dictionary<string, object> { ["jsonrpc"] = "2.0", ["id"] = id, ["result"] = result });
        }

        private static void Notify(Stream output, string method, object parameters)
        {
            Send(output, new Dictionary<string, object> { ["jsonrpc"] = "2.0", ["method"] = method, ["params"] = parameters });
        }

        private static void Send(Stream output, object value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(Json.Serialize(value));
            byte[] header = Encoding.ASCII.GetBytes("Content-Length: " + bytes.Length + "\r\n\r\n");
            output.Write(header, 0, header.Length);
            output.Write(bytes, 0, bytes.Length);
            output.Flush();
        }
    }

    internal struct Position
    {
        public int Line;
        public int Character;
        public Position(int line, int character) { Line = line; Character = character; }
    }

    internal sealed class Symbol
    {
        public string Name;
        public string Kind;
        public string Type;
        public string Detail;
        public int Start;
        public int Length;
        public int Line;
        public int Column;
        public int EndLine;
        public int EndColumn;
        public List<Parameter> Parameters = new List<Parameter>();
    }

    internal sealed class Parameter
    {
        public string Name;
        public string Type;
    }

    internal sealed class Document
    {
        private static readonly string[] Keywords = { "import", "export", "namespace", "struct", "using", "uniform", "layout", "fn", "const", "void", "if", "else", "while", "do", "for", "return", "true", "false", "readonly", "writeonly" };
        private static readonly string[] BuiltinTypes = { "void", "bool", "i32", "u32", "f32", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4", "mat2", "mat3", "mat4", "Sampler2D", "SamplerCube", "UniformBuffer", "StorageBuffer" };
        private static readonly string[] BuiltinFunctions = { "abs", "floor", "fract", "sqrt", "min", "max", "mod", "mix", "smoothstep", "float_bits_to_uint", "texture_size", "sample" };
        private readonly string uri;
        private readonly string text;
        private readonly List<Symbol> symbols = new List<Symbol>();
        private readonly List<Token> tokens = new List<Token>();
        private readonly Dictionary<string, string> fields = new Dictionary<string, string>(StringComparer.Ordinal);

        private sealed class Token { public string Text; public int Start; public int Line; public int Column; public int Length { get { return Text.Length; } } }

        public Document(string documentUri, string documentText)
        {
            uri = documentUri;
            text = documentText ?? string.Empty;
            Lex();
            Index();
        }

        private void Lex()
        {
            int i = 0, line = 0, column = 0;
            while (i < text.Length)
            {
                char c = text[i];
                if (c == '\n') { i++; line++; column = 0; continue; }
                if (char.IsWhiteSpace(c)) { i++; column++; continue; }
                if (c == '/' && i + 1 < text.Length && text[i + 1] == '/')
                {
                    int start = i; while (i < text.Length && text[i] != '\n') { i++; column++; }
                    tokens.Add(new Token { Text = text.Substring(start, i - start), Start = start, Line = line, Column = column - (i - start) }); continue;
                }
                int tokenStart = i, tokenColumn = column;
                if (char.IsLetter(c) || c == '_')
                {
                    i++; column++; while (i < text.Length && (char.IsLetterOrDigit(text[i]) || text[i] == '_')) { i++; column++; }
                }
                else if (char.IsDigit(c))
                {
                    i++; column++; while (i < text.Length && (char.IsLetterOrDigit(text[i]) || text[i] == '.')) { i++; column++; }
                }
                else if (c == '"')
                {
                    i++; column++; while (i < text.Length && text[i] != '"' && text[i] != '\n') { if (text[i] == '\\' && i + 1 < text.Length) { i += 2; column += 2; } else { i++; column++; } } if (i < text.Length && text[i] == '"') { i++; column++; }
                }
                else
                {
                    i++; column++; if (i < text.Length && ((c == ':' && text[i] == ':') || (c == '-' && text[i] == '>') || (c == '=' && text[i] == '=') || (c == '!' && text[i] == '='))) { i++; column++; }
                }
                tokens.Add(new Token { Text = text.Substring(tokenStart, i - tokenStart), Start = tokenStart, Line = line, Column = tokenColumn });
            }
        }

        private void Index()
        {
            for (int i = 0; i + 1 < tokens.Count; i++)
            {
                Token token = tokens[i];
                if (token.Text == "struct" && i + 1 < tokens.Count && IsIdentifier(tokens[i + 1].Text))
                {
                    Symbol symbol = Add(tokens[i + 1], "struct", tokens[i + 1].Text, "struct " + tokens[i + 1].Text);
                    int brace = FindNext(i + 2, "{");
                    if (brace >= 0) IndexFields(brace, symbol.Name);
                }
                if (token.Text == "fn" && i + 1 < tokens.Count)
                {
                    int nameIndex = i + 1;
                    if (!IsIdentifier(tokens[nameIndex].Text)) continue;
                    int open = FindNext(nameIndex + 1, "(");
                    Symbol function = Add(tokens[nameIndex], "function", ReturnType(open), Signature(nameIndex, open));
                    if (open >= 0) ParseParameters(function, open);
                }
                if ((token.Text == "uniform" || token.Text == "layout") && i + 2 < tokens.Count)
                {
                    if (token.Text == "uniform" && IsIdentifier(tokens[i + 1].Text) && tokens[i + 2].Text == "{")
                    {
                        Add(tokens[i + 1], "namespace", "uniform", "uniform " + tokens[i + 1].Text);
                    }
                    else if (token.Text == "uniform" && IsType(tokens[i + 1].Text) && IsIdentifier(tokens[i + 2].Text))
                    {
                        Add(tokens[i + 2], "variable", tokens[i + 1].Text, tokens[i + 1].Text + " " + tokens[i + 2].Text);
                    }
                    else if (token.Text == "layout")
                    {
                        int colon = FindNext(i + 1, ":");
                        if (colon >= 0 && colon + 1 < tokens.Count && IsIdentifier(tokens[colon + 1].Text))
                            Add(tokens[colon + 1], "field", "layout", "layout " + tokens[colon + 1].Text);
                    }
                }
            }
            for (int i = 0; i + 2 < tokens.Count; i++)
            {
                if (IsType(tokens[i].Text) && IsIdentifier(tokens[i + 1].Text) && (tokens[i + 2].Text == ";" || tokens[i + 2].Text == "="))
                    Add(tokens[i + 1], "variable", tokens[i].Text, tokens[i].Text + " " + tokens[i + 1].Text);
            }
        }

        private void IndexFields(int brace, string owner)
        {
            for (int i = brace + 1; i + 2 < tokens.Count && tokens[i].Text != "}"; i++)
            {
                if (IsType(tokens[i].Text) && IsIdentifier(tokens[i + 1].Text) && tokens[i + 2].Text == ";")
                    fields[owner + "." + tokens[i + 1].Text] = tokens[i].Text;
            }
        }

        private Symbol Add(Token token, string kind, string type, string detail)
        {
            Symbol symbol = new Symbol { Name = token.Text, Kind = kind, Type = type, Detail = detail, Start = token.Start, Length = token.Length, Line = token.Line, Column = token.Column };
            symbol.EndLine = token.Line; symbol.EndColumn = token.Column + token.Length; symbols.Add(symbol); return symbol;
        }

        private int FindNext(int start, string value) { for (int i = start; i < tokens.Count; i++) if (tokens[i].Text == value) return i; return -1; }
        private string ReturnType(int open) { int arrow = open < 0 ? -1 : FindNext(open, "->"); return arrow >= 0 && arrow + 1 < tokens.Count ? tokens[arrow + 1].Text : "void"; }
        private string Signature(int name, int open) { if (open < 0) return "fn " + tokens[name].Text; int close = FindNext(open, ")"); return "fn " + tokens[name].Text + text.Substring(tokens[open].Start, close < 0 ? text.Length - tokens[open].Start : tokens[close].Start + 1 - tokens[open].Start); }
        private void ParseParameters(Symbol symbol, int open) { for (int i = open + 1; i + 1 < tokens.Count && tokens[i].Text != ")"; i++) if (IsType(tokens[i].Text) && IsIdentifier(tokens[i + 1].Text)) { symbol.Parameters.Add(new Parameter { Type = tokens[i].Text, Name = tokens[i + 1].Text }); i++; } }
        private static bool IsIdentifier(string value) { return value.Length != 0 && (char.IsLetter(value[0]) || value[0] == '_') && value != "true" && value != "false"; }
        private bool IsType(string value) { return BuiltinTypes.Contains(value) || symbols.Any(s => s.Kind == "struct" && s.Name == value); }

        public object[] Diagnostics()
        {
            List<object> result = new List<object>();
            int braces = 0, parentheses = 0;
            foreach (Token token in tokens)
            {
                if (token.Text == "{") braces++; if (token.Text == "}") braces--; if (token.Text == "(") parentheses++; if (token.Text == ")") parentheses--;
                if (braces < 0 || parentheses < 0) { result.Add(Diagnostic(token.Line, token.Column, token.Length, "unexpected closing delimiter")); braces = Math.Max(0, braces); parentheses = Math.Max(0, parentheses); }
            }
            if (braces > 0) result.Add(Diagnostic(tokens.Count == 0 ? 0 : tokens[tokens.Count - 1].Line, 0, 1, "unclosed block"));
            if (parentheses > 0) result.Add(Diagnostic(tokens.Count == 0 ? 0 : tokens[tokens.Count - 1].Line, 0, 1, "unclosed parameter list"));
            return result.ToArray();
        }

        private object Diagnostic(int line, int column, int length, string message) { return new Dictionary<string, object> { ["range"] = Range(line, column, line, column + length), ["severity"] = 1, ["source"] = "RTSL", ["message"] = message }; }
        private static Dictionary<string, object> Range(int sl, int sc, int el, int ec) { return new Dictionary<string, object> { ["start"] = new Dictionary<string, object> { ["line"] = sl, ["character"] = sc }, ["end"] = new Dictionary<string, object> { ["line"] = el, ["character"] = ec } }; }
        private Dictionary<string, object> Location(Symbol symbol) { return new Dictionary<string, object> { ["uri"] = uri, ["range"] = Range(symbol.Line, symbol.Column, symbol.EndLine, symbol.EndColumn) }; }

        public int[] SemanticTokens()
        {
            List<int> data = new List<int>(); int previousLine = 0, previousColumn = 0;
            foreach (Token token in tokens)
            {
                int type = Array.IndexOf(Keywords, token.Text) >= 0 ? 0 : IsType(token.Text) ? 6 : symbols.Any(s => s.Name == token.Text && s.Kind == "function") ? 7 : char.IsDigit(token.Text[0]) ? 2 : token.Text.StartsWith("\"") ? 1 : 4;
                data.Add(token.Line - previousLine); data.Add(token.Line == previousLine ? token.Column - previousColumn : token.Column); data.Add(token.Length); data.Add(type); data.Add(0); previousLine = token.Line; previousColumn = token.Column;
            }
            return data.ToArray();
        }

        private string WordAt(Position position)
        {
            Token token = tokens.FirstOrDefault(t => t.Line == position.Line && position.Character >= t.Column && position.Character <= t.Column + t.Length);
            return token == null ? null : token.Text;
        }

        public object Completion(Position position)
        {
            List<object> items = new List<object>(); string prefix = WordAt(position) ?? string.Empty;
            string line = GetLine(position.Line);
            int before = Math.Min(position.Character, line.Length);
            int dot = line.LastIndexOf('.', Math.Max(0, before - 1));
            if (dot >= 0 && dot < before)
            {
                string owner = PreviousIdentifier(line, dot - 1);
                string memberPrefix = line.Substring(dot + 1, before - dot - 1);
                foreach (KeyValuePair<string, string> field in fields.Where(f => f.Key.StartsWith(owner + ".", StringComparison.Ordinal)))
                {
                    string name = field.Key.Substring(owner.Length + 1);
                    if (name.StartsWith(memberPrefix, StringComparison.Ordinal))
                        items.Add(Item(name, 10, field.Value + " " + name));
                }
                return new Dictionary<string, object> { ["isIncomplete"] = false, ["items"] = items.ToArray() };
            }
            foreach (string keyword in Keywords) if (keyword.StartsWith(prefix, StringComparison.Ordinal)) items.Add(Item(keyword, 14, keyword));
            foreach (string type in BuiltinTypes) if (type.StartsWith(prefix, StringComparison.Ordinal)) items.Add(Item(type, 7, type));
            foreach (string function in BuiltinFunctions) if (function.StartsWith(prefix, StringComparison.Ordinal)) items.Add(Item(function, 3, function + "(...)", function + "($0)"));
            foreach (Symbol symbol in symbols.Where(s => s.Name.StartsWith(prefix, StringComparison.Ordinal))) items.Add(Item(symbol.Name, symbol.Kind == "function" ? 3 : symbol.Kind == "struct" ? 7 : 6, symbol.Detail));
            return new Dictionary<string, object> { ["isIncomplete"] = false, ["items"] = items.ToArray() };
        }

        private static Dictionary<string, object> Item(string label, int kind, string detail) { return Item(label, kind, detail, label); }
        private static Dictionary<string, object> Item(string label, int kind, string detail, string insertText) { return new Dictionary<string, object> { ["label"] = label, ["kind"] = kind, ["detail"] = detail, ["insertText"] = insertText }; }
        private string GetLine(int line)
        {
            string[] lines = text.Split(new[] { "\n" }, StringSplitOptions.None);
            return line >= 0 && line < lines.Length ? lines[line].TrimEnd('\r') : string.Empty;
        }
        private static string PreviousIdentifier(string line, int end)
        {
            while (end >= 0 && char.IsWhiteSpace(line[end])) end--;
            int start = end;
            while (start >= 0 && (char.IsLetterOrDigit(line[start]) || line[start] == '_')) start--;
            return start < end ? line.Substring(start + 1, end - start) : string.Empty;
        }
        public object Hover(Position position) { Symbol symbol = symbols.FirstOrDefault(s => s.Name == WordAt(position)); return symbol == null ? null : new Dictionary<string, object> { ["contents"] = new Dictionary<string, object> { ["kind"] = "markdown", ["value"] = "```rtsl\n" + symbol.Detail + "\n```" }, ["range"] = Range(symbol.Line, symbol.Column, symbol.EndLine, symbol.EndColumn) }; }
        public object[] Definition(Position position) { Symbol symbol = symbols.FirstOrDefault(s => s.Name == WordAt(position)); return symbol == null ? new object[0] : new[] { Location(symbol) }; }
        public object[] DocumentSymbols() { return symbols.Select(s => new Dictionary<string, object> { ["name"] = s.Name, ["kind"] = s.Kind == "function" ? 12 : s.Kind == "struct" ? 23 : 13, ["detail"] = s.Detail, ["range"] = Range(s.Line, s.Column, s.EndLine, s.EndColumn), ["selectionRange"] = Range(s.Line, s.Column, s.EndLine, s.EndColumn) }).Cast<object>().ToArray(); }
        public object SignatureHelp(Position position) { string name = WordAt(position); Symbol symbol = symbols.FirstOrDefault(s => s.Name == name && s.Kind == "function"); if (symbol == null) return null; return new Dictionary<string, object> { ["signatures"] = new[] { new Dictionary<string, object> { ["label"] = symbol.Detail + " -> " + symbol.Type, ["parameters"] = symbol.Parameters.Select(p => (object)new Dictionary<string, object> { ["label"] = p.Type + " " + p.Name }).ToArray() } }, ["activeSignature"] = 0, ["activeParameter"] = 0 }; }
    }
}
