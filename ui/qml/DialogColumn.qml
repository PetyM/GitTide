import QtQuick
import QtQuick.Layouts

// Content wrapper for an AppDialog whose body is a vertical stack. A ColumnLayout
// assigned directly as a Dialog's `contentItem` does not propagate its implicit
// height (the Dialog then sizes too short and the content overflows the card);
// this wraps the column in a plain Item that reports the column's implicit size,
// which AppDialog folds into its height.
//
// Drop-in replacement for a `contentItem: ColumnLayout`: set `spacing` and declare
// the body items as children exactly as before. The inner column is assigned via
// `children` (not the default property) so it is not captured by the `content`
// alias — user-declared items land in the column, the column lands in this Item.
Item {
    id: wrapper
    default property alias content: col.data
    property alias spacing: col.spacing

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    children: [
        ColumnLayout {
            id: col
            width: wrapper.width
        }
    ]
}
