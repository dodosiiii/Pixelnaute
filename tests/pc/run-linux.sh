#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cc=${CC:-cc}
cxx=${CXX:-c++}
cflags=(-O2 -std=c11 -I"$root/tests/pc" -I"$root/source")
cxxflags=(-O2 -std=c++17 -I"$root/tests/pc" -I"$root/source")

"$cc" "${cflags[@]}" -c "$root/source/render/stb_truetype_impl.c" -o "$tmp/stb_truetype_impl.o"
"$cc" "${cflags[@]}" -c "$root/source/image/stb_impl.c" -o "$tmp/stb_impl.o"

"$cxx" "${cxxflags[@]}" \
	"$root/tests/pc/main_pc.cpp" \
	"$root/source/render/text.cpp" \
	"$root/source/render/bitmap.cpp" \
	"$root/source/render/layout.cpp" \
	"$root/source/css/css.cpp" \
	"$root/source/html/dom.cpp" \
	"$root/source/html/parser.cpp" \
	"$root/source/network/url.cpp" \
	"$root/source/image/image_store.cpp" \
	"$root/source/network/encoding.cpp" \
	"$root/source/pdf/pdf_reader.cpp" \
	"$root/source/search/search.cpp" \
	"$root/source/search/search_page.cpp" \
	"$tmp/stb_truetype_impl.o" "$tmp/stb_impl.o" \
	-lm -o "$tmp/pc_test"

cd "$tmp"
./pc_test
