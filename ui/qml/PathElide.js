.pragma library

// Progressive directory elide: collapse the leftmost directory segments to their
// first character, one at a time, only as far as needed to make "dir + name" fit
// the available width. "/some/long/path/to/" + "file.txt" degrades through
// "/s/long/path/to/", "/s/l/path/to/", … down to "/s/l/p/t/" — the file name is
// never touched. See ChangesPane.qml / CommitDetail.qml for use.

// Split "a/b/c/" into {leading: false, segs: ["a","b","c"]}; remembers a leading
// slash so an absolute path keeps its root marker.
function _parts(dir)
{
    var leading = dir.charAt(0) === "/"
    var segs = dir.split("/").filter(function (s) { return s.length > 0 })
    return { leading: leading, segs: segs }
}

// Rebuild the "dir/" prefix with the leftmost `k` segments collapsed to one char.
function _build(p, k)
{
    var out = p.leading ? "/" : ""
    for (var i = 0; i < p.segs.length; i++)
        out += (i < k ? p.segs[i].charAt(0) : p.segs[i]) + "/"
    return out
}

// Least-abbreviated "dir/" prefix that, with `name` appended, fits `avail` px.
// `measure(text)` must return the pixel width of `text` in the target font.
// Returns the fully-collapsed prefix when even that overflows (caller's elide
// is the final backstop). Empty dir -> empty string.
function fit(dir, name, avail, measure)
{
    if (!dir)
        return ""
    var p = _parts(dir)
    for (var k = 0; k <= p.segs.length; k++)
    {
        var cand = _build(p, k)
        if (k === p.segs.length || measure(cand + name) <= avail)
            return cand
    }
    return _build(p, p.segs.length)
}
