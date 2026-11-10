#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGET=${1:-host}
source "$ROOT/scripts/native-target.sh" "$TARGET"
python3 "$ROOT/scripts/prepare-web-search.py"
SOURCE="$ROOT/.deps/edge-curl-impersonate-source"
CACHE=${PENCHAT_EDGE_SOURCE_CACHE:-/home/ls-jl/Documents/ChatGPT/web search/edge-search/.host-deps/impersonate-source-cache}
if [ ! -f "$SOURCE/CMakeLists.txt" ]; then
  python3 "$ROOT/native/web-search/upstream/tools/prepare_impersonate_source.py" --cache-dir "$CACHE" --output-dir "$ROOT/.deps/edge-source-bundles"
  python3 - "$ROOT" <<'PY'
import pathlib,tarfile,json,hashlib
import sys
root=pathlib.Path(sys.argv[1]);archive=root/'.deps/edge-source-bundles/edge-curl-impersonate-2.3.2-edge2.tar'
expected='5a065dcce9415d4ae7a07260d3267f20d43a2c122d92630baba526b4da5554de'
if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:raise ValueError('Unexpected pinned native transport source bundle')
target=root/'.deps/edge-curl-impersonate-source';target.mkdir(parents=True,exist_ok=True)
with tarfile.open(archive) as tar:
 for member in tar.getmembers():
  parts=pathlib.PurePosixPath(member.name).parts[1:]
  if not parts:continue
  if member.issym() or member.islnk() or '..' in parts or member.name.startswith('/'):raise ValueError('Unsafe source archive')
  dest=target.joinpath(*parts)
  if member.isdir():dest.mkdir(parents=True,exist_ok=True)
  elif member.isfile():dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(tar.extractfile(member).read());dest.chmod(member.mode&0o777)
PY
fi
PREFIX="$ROOT/.deps/web-search-$TARGET"
ARGS=()
if [ "$TARGET" != host ]; then
  export PENCHAT_WEB_GCC="$PENCHAT_BUILD_GCC" PENCHAT_WEB_TRIPLE="$PENCHAT_BUILD_TRIPLE"
  export PENCHAT_WEB_CXX_HEADERS="${PENCHAT_EDGE_CXX_HEADERS:-/home/ls-jl/Documents/ChatGPT/web search/edge-search/.host-deps/cross/root/usr/$PENCHAT_BUILD_TRIPLE/include/c++/12}"
  TOOLCHAIN="$ROOT/.deps/web-search-$TARGET-toolchain.cmake"
  cat > "$TOOLCHAIN" <<CMAKE
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $PENCHAT_BUILD_CPU)
set(CMAKE_C_COMPILER "$ROOT/scripts/web-search-cc.py")
set(CMAKE_ASM_COMPILER "$ROOT/scripts/web-search-cc.py")
set(CMAKE_CXX_COMPILER "$ROOT/scripts/web-search-cxx.py")
CMAKE
  ARGS=(-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN")
fi
# A source-built, privately namespaced transport; never use host/cross experiment binaries.
cmake -S "$SOURCE" -B "$ROOT/build/web-transport-$TARGET" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX/transport" -DUSE_LIBIDN2=OFF -DCURL_SUBPROJECT_FORCE_OPENSSL_PATHS=ON -DBUILD_SHARED_LIBS=OFF "${ARGS[@]}"
cmake --build "$ROOT/build/web-transport-$TARGET" -j"${JOBS:-6}"
# The locked namespace adapter relocates every curl symbol and matching headers.
python3 "$ROOT/native/web-search/upstream/tools/namespace_impersonate.py" --help >/dev/null
cmake --install "$ROOT/build/web-transport-$TARGET"
NAMESPACE_ARGS=()
if [ "$TARGET" != host ]; then
  NAMESPACE_ARGS=(--cxx "$ROOT/scripts/web-search-cxx.py" --nm "$PENCHAT_BUILD_GCC/bin/$PENCHAT_BUILD_TRIPLE-nm" --objcopy "$PENCHAT_BUILD_GCC/bin/$PENCHAT_BUILD_TRIPLE-objcopy" --strip "$PENCHAT_BUILD_STRIP")
fi
python3 "$ROOT/native/web-search/upstream/tools/namespace_impersonate.py" \
  --source-prefix "$PREFIX/transport" --output-prefix "$PREFIX/private" "${NAMESPACE_ARGS[@]}"
python3 "$ROOT/scripts/prepare-web-xml.py"
cmake -S "$ROOT/.deps/web-libxml2" -B "$ROOT/build/web-xml-$TARGET" -DCMAKE_INSTALL_PREFIX="$PREFIX/xml" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF \
  -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_LZMA=OFF \
  -DLIBXML2_WITH_ZLIB=OFF -DLIBXML2_WITH_HTTP=OFF -DLIBXML2_WITH_FTP=OFF "${ARGS[@]}"
cmake --build "$ROOT/build/web-xml-$TARGET" -j"${JOBS:-6}"
cmake --install "$ROOT/build/web-xml-$TARGET"
cmake -S "$ROOT/native/web-search" -B "$ROOT/build/web-search-$TARGET" -DCMAKE_BUILD_TYPE=Release \
  -DEDGE_SEARCH_IMPERSONATE_ROOT="$PREFIX/private" -DLIBXML2_INCLUDE_DIR="$PREFIX/xml/include/libxml2" \
  -DLIBXML2_LIBRARY="$PREFIX/xml/lib/libxml2.a" "${ARGS[@]}"
cmake --build "$ROOT/build/web-search-$TARGET" --target penchat-web-search -j"${JOBS:-6}"
STAGE="$ROOT/build/payload-$TARGET/assets"
mkdir -p "$STAGE/bin" "$STAGE/web-search/lib" "$STAGE/web-search/licenses"
install -m755 "$ROOT/build/web-search-$TARGET/penchat-web-search" "$STAGE/bin/"
rm -f "$STAGE/web-search/lib/"*.so*
cp -L "$PREFIX/private/lib/libedge-curl-impersonate.so.4" "$STAGE/web-search/lib/"
cp "$ROOT/native/web-search/upstream/LICENSE" "$STAGE/web-search/licenses/Edge-Search-MIT.txt"
cp "$ROOT/native/web-search/upstream/THIRD_PARTY_NOTICES.txt" "$STAGE/web-search/licenses/"
cp -a "$SOURCE/licenses/". "$STAGE/web-search/licenses/"
cp -a "$ROOT/native/web-search/upstream/licenses/". "$STAGE/web-search/licenses/"
cp "$ROOT/.deps/web-libxml2/Copyright" "$STAGE/web-search/licenses/libxml2.txt"
cp "$ROOT/native/web-search/source-lock.json" "$STAGE/web-search/"
if [ "$TARGET" != host ]; then
  "$PENCHAT_BUILD_STRIP" --strip-unneeded "$STAGE/bin/penchat-web-search"
  for file in "$STAGE/web-search/lib/"*; do "$PENCHAT_BUILD_STRIP" --strip-unneeded "$file"; done
fi
if [ "$TARGET" = rk3562 ]; then
  cp "$STAGE/bin/penchat-web-search" "$ROOT/assets/bin/"
  rm -rf "$ROOT/assets/web-search";cp -a "$STAGE/web-search" "$ROOT/assets/"
fi
