#!/bin/bash
# Usage: gen_hdr.sh <dir> <proto-lines>
dir="$1"; shift
{
echo "// Declares this plugin's effect implementations. Impl files assign"
echo "// bodies to these prototypes; exports.cc binds them into the ABI"
echo "// table. Video fns: BGRA8 in-place, return 0 on success."
echo "#pragma once"
echo ""
echo "#include \"../../sdk/xer_plugin.h\""
echo ""
echo "namespace fx {"
echo "$@"
echo ""
echo "}  // namespace fx"
} > "$dir/effects.h"
