using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;

namespace RTSL.VisualStudio
{
[Export(typeof(IClassifierProvider)), ContentType(RtslContentType.Name)]
internal sealed class RtslLexicalClassifierProvider : IClassifierProvider
{
    [Import] internal IClassificationTypeRegistryService Types = null!;
    public IClassifier GetClassifier(ITextBuffer buffer) => buffer.Properties.GetOrCreateSingletonProperty(() => new RtslLexicalClassifier(buffer, Types));
}

internal sealed class RtslLexicalClassifier : IClassifier
{
    [StructLayout(LayoutKind.Sequential)] private struct Span { public uint Offset, Length, Category; }
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint LexicalSpans(byte[] text, uint length, [Out] Span[] spans, uint capacity);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    private static readonly LexicalSpans Lex = LoadLexer();
    private readonly ITextBuffer buffer;
    private readonly IClassificationTypeRegistryService types;
    private ITextSnapshot cachedSnapshot = null!;
    private IList<Span> cachedSpans = Array.Empty<Span>();
    public event EventHandler<ClassificationChangedEventArgs> ClassificationChanged;
    public RtslLexicalClassifier(ITextBuffer buffer, IClassificationTypeRegistryService types)
    {
        this.buffer = buffer;
        this.types = types;
        buffer.Changed += OnBufferChanged;
    }
    private void OnBufferChanged(object sender, TextContentChangedEventArgs args)
    {
        cachedSnapshot = null!;
        ClassificationChanged?.Invoke(this, new ClassificationChangedEventArgs(new SnapshotSpan(args.After, 0, args.After.Length)));
    }
    public IList<ClassificationSpan> GetClassificationSpans(SnapshotSpan requested)
    {
        if (cachedSnapshot != requested.Snapshot) Refresh(requested.Snapshot);
        var result = new List<ClassificationSpan>();
        foreach (var item in cachedSpans) {
            if (item.Offset + item.Length > requested.Snapshot.Length) continue;
            var span = new SnapshotSpan(requested.Snapshot, (int)item.Offset, (int)item.Length);
            if (!span.IntersectsWith(requested)) continue;
            var name = item.Category == 1 ? "keyword" : item.Category == 2 ? RtslLexicalColors.ControlKeyword : item.Category == 3 ? "number" : item.Category == 4 ? "string" : item.Category == 5 ? "operator" : item.Category == 6 ? RtslLexicalColors.Comment : item.Category == 7 ? RtslLexicalColors.Attribute : "text";
            var type = types.GetClassificationType(name)
                ?? types.CreateClassificationType(name, new[] { types.GetClassificationType("text")! });
            result.Add(new ClassificationSpan(span, type));
        }
        return result;
    }
    private void Refresh(ITextSnapshot snapshot)
    {
        var text = snapshot.GetText();
        var utf8 = Encoding.UTF8.GetBytes(text);
        var native = new Span[Math.Max(64, utf8.Length)];
        var count = Lex(utf8, (uint)utf8.Length, native, (uint)native.Length);
        var byteToCharacter = BuildByteToCharacterMap(text, utf8.Length);
        var converted = new List<Span>((int)count);
        for (var index = 0; index < count; ++index) {
            var item = native[index];
            if (item.Offset > utf8.Length || item.Length > utf8.Length - item.Offset) continue;
            var start = byteToCharacter[item.Offset];
            var end = byteToCharacter[item.Offset + item.Length];
            if (end > start) converted.Add(new Span { Offset = (uint)start, Length = (uint)(end - start), Category = item.Category });
        }
        cachedSpans = converted;
        cachedSnapshot = snapshot;
    }
    private static int[] BuildByteToCharacterMap(string text, int byteLength)
    {
        var map = new int[byteLength + 1];
        var byteOffset = 0;
        for (var characterOffset = 0; characterOffset < text.Length;) {
            var characterCount = char.IsHighSurrogate(text[characterOffset]) && characterOffset + 1 < text.Length && char.IsLowSurrogate(text[characterOffset + 1]) ? 2 : 1;
            var bytes = Encoding.UTF8.GetByteCount(text.Substring(characterOffset, characterCount));
            for (var index = 0; index < bytes; ++index) map[byteOffset + index] = characterOffset;
            byteOffset += bytes;
            map[byteOffset] = characterOffset + characterCount;
            characterOffset += characterCount;
        }
        return map;
    }
    private static LexicalSpans LoadLexer()
    {
        var directory = System.IO.Path.GetDirectoryName(typeof(RtslLexicalClassifier).Assembly.Location)!;
        var module = LoadLibrary(System.IO.Path.Combine(directory, "rtsl-lexer.dll"));
        if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "The RTSL lexer bridge was not packaged with this VSIX.");
        var entryPoint = GetProcAddress(module, "rtsl_lexical_spans");
        if (entryPoint == IntPtr.Zero) throw new EntryPointNotFoundException("rtsl_lexical_spans");
        return (LexicalSpans)Marshal.GetDelegateForFunctionPointer(entryPoint, typeof(LexicalSpans));
    }
}
}
