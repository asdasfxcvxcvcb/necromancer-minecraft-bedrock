cmake_minimum_required(VERSION 3.20)

# Emits a lookup table binding Minecraft item ids to the item textures embedded by
# ADD_RESOURCES. The D2D renderer needs real bitmap data because it cannot call the
# game's item renderer, which is what AntiObs relies on.

if (NOT DEFINED ITEM_DIR OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "ITEM_DIR and OUTPUT_FILE are required")
endif ()

file(GLOB ITEM_PNGS RELATIVE "${ITEM_DIR}" "${ITEM_DIR}/*.png")
list(SORT ITEM_PNGS)

set(DECLS "")
set(ROWS "")

foreach (png ${ITEM_PNGS})
    string(REGEX REPLACE "\\.png$" "" item_id "${png}")

    # ld derives its symbol from the full relative path with every non-alphanumeric
    # character replaced by an underscore.
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" symbol "assets/items/${png}")

    string(APPEND DECLS "extern \"C\" const char _binary_${symbol}_start[];\n")
    string(APPEND DECLS "extern \"C\" const char _binary_${symbol}_end[];\n")

    string(APPEND ROWS "    { \"${item_id}\", _binary_${symbol}_start, _binary_${symbol}_end },\n")
endforeach ()

list(LENGTH ITEM_PNGS ITEM_COUNT)

file(WRITE "${OUTPUT_FILE}"
"#include \"client/render/asset/ItemIconRegistry.h\"

${DECLS}
namespace {
    const ItemIconRegistry::Entry kEntries[] = {
${ROWS}    };
}

std::span<const ItemIconRegistry::Entry> ItemIconRegistry::entries() {
    return { kEntries, ${ITEM_COUNT} };
}
")

message(STATUS "Item icon registry: ${ITEM_COUNT} textures")
