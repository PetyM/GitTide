import QtQuick

// One diff row's code column. A read-only TextEdit rather than a Label because
// only TextEdit can map a pixel to a character index (positionAt) and paint a
// selection (select) — both of which the shared DiffSelection needs.
//
// Presentational only: pointer and key handling live in DiffSelectionOverlay,
// which sits outside the delegates and therefore survives autoscroll recycling.
//
// TextEdit has no elide, so a long line is hard-clipped without the "…" a Label
// would draw. Copy still yields the full line — it reads the model, not this item.
TextEdit {
    id: codeText
    objectName: "diffCodeText"

    // Model row this item renders. Selection ranges are looked up by it.
    property int row: -1
    // The DiffSelection this row participates in (null = never selected).
    property var selection: null
    // Syntax-highlighted HTML for the row; empty means render plainText.
    property string html: ""
    // The row's source text, as the model holds it.
    property string plainText: ""

    readonly property int selFrom: selection && selection.hasSelection ? selection.startInRow(row) : -1
    readonly property int selTo:   selection && selection.hasSelection ? selection.endInRow(row) : -1

    readOnly: true
    selectByMouse: false
    selectByKeyboard: false
    activeFocusOnPress: false
    clip: true
    font.family: "monospace"
    font.pixelSize: 12
    textFormat: html.length > 0 ? Text.RichText : Text.PlainText
    // QTextDocument's HTML parser collapses leading indentation and runs of
    // internal spaces by default, which would shift every document position
    // (positionAt/select) away from the source-string column the highlighted
    // fragment came from — wrapping it in a `white-space:pre` span keeps
    // document positions 1:1 with plainText's column offsets, the same
    // mapping DiffSelection's row/col coordinates already assume.
    text: html.length > 0 ? ('<span style="white-space:pre">' + html + '</span>') : plainText
    selectionColor: theme.selectionBg
    selectedTextColor: theme.selectionText
    // RowLayout centres non-fillHeight siblings (the gutter/sign Labels)
    // vertically in the row; this item opts into Layout.fillHeight so its
    // clip region covers the whole row, so it must centre its own text the
    // same way or it reads a couple of pixels higher than its neighbours.
    verticalAlignment: TextEdit.AlignVCenter

    function applySelection() {
        if (selFrom < 0 || selTo <= selFrom)
            codeText.deselect()
        else
            codeText.select(selFrom, selTo)
    }

    onSelFromChanged: applySelection()
    onSelToChanged: applySelection()
    // Reapply on the row's own source properties, not TextEdit's `text` — that
    // signal also fires from inside select()/deselect() itself (the document
    // treats a selection/format change as a content change), which would
    // recurse into applySelection() through this same handler forever.
    onPlainTextChanged: applySelection()
    onHtmlChanged: applySelection()
    Component.onCompleted: applySelection()
}
