#!/usr/bin/env bash
# Build current RK/CVI packages on a configured local machine. Publish only on request.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ $# -gt 1 || (${1:-} != "" && ${1:-} != --publish) ]]; then
  printf 'Usage: bash scripts/publish-dev-amr.sh [--publish]\n' >&2
  exit 2
fi
if [[ -n $(git status --porcelain) ]]; then
  printf 'Commit source changes before producing a commit-labelled build.\n' >&2
  exit 2
fi

commit=$(git rev-parse HEAD)
short_commit=${commit:0:12}
read -r appid version < <(node -p 'const p=require("./package.json"); p.appid+" "+p.version')
tag="dev-amr-$version-$short_commit"
mkdir -p .work/dev-amr
exec 9>.work/dev-amr/build.lock
flock --nonblock 9 || { printf 'A local Dev AMR build is running.\n' >&2; exit 2; }
output=$(mktemp -d "$PWD/.work/dev-amr/$tag.XXXXXX")
printf '%s\n' "$commit" > "$output/SOURCE_COMMIT"
export PENCHAT_PUBLIC_DEV_BUILD=1

for target in rk3562 cv1826-s; do
  printf 'Building %s from %s. Log: %s/%s.log\n' "$target" "$short_commit" "$output" "$target"
  {
    bash scripts/build-lvgl.sh "$target"
    bash scripts/build-web-search.sh "$target"
    bash scripts/build-scanner.sh "$target"
  } > "$output/$target.log" 2>&1
done

{
  python3 scripts/package-dev.py rk3562
  python3 scripts/package-cv1826-s-dev.py --no-build
} > "$output/package.log" 2>&1

# Both normal and rescue binaries use this generated header. Check that public
# builds really ignored any developer feedback destination or credential.
expected_feedback=$(cat <<'EOF'
#pragma once
namespace penchat {
static const char FeedbackBuild_endpoint[] = "";
static const char FeedbackBuild_token[] = "";
static const char FeedbackBuild_nodes[] = "[]";
}
EOF
)
for target in rk3562 cv1826-s; do
  cmp -s "build/$target/generated/feedback_private.hpp" <(printf '%s\n' "$expected_feedback") ||
    { printf 'Unexpected feedback configuration in %s.\n' "$target" >&2; exit 2; }
done

test "$(git rev-parse HEAD)" = "$commit"
test -z "$(git status --porcelain)"
assets=()
for target in rk3562 cv1826-s; do
  native_name="$appid.${version//./_}-$target.amr"
  asset="PenChat-$version-dev-$short_commit-$target.amr"
  cp "build/package-$target/$native_name" "$output/$asset"
  assets+=("$asset")
done
(cd "$output" && sha256sum "${assets[@]}" SOURCE_COMMIT > SHA256SUMS && sha256sum -c SHA256SUMS)
printf 'Validated Dev files: %s\n' "$output"

if [[ ${1:-} != --publish ]]; then
  exit 0
fi

repo=$(gh repo view "$(git remote get-url origin)" --json nameWithOwner --jq .nameWithOwner)
if gh release view "$tag" --repo "$repo" >/dev/null 2>&1; then
  printf 'Release %s already exists. Existing files were left in place.\n' "$tag" >&2
  exit 2
fi
# Push only the exact snapshot tag. An existing different remote tag will fail.
git push origin "${commit}:refs/tags/${tag}"

cat > "$output/release-note.md" <<EOF
## 词典笔 Dev 安装包

- RK3562（Y01、Y02、Y07）选择以 rk3562.amr 结尾的文件。
- CV1826（S6）选择以 cv1826-s.amr 结尾的文件。
- 使用对应的 MiniApp 安装工具手动安装。应用内版本仍是 $version。
- 两个包均完成身份、架构、完整性和运行时无 shell 校验。本次未做独立实机验收。
- 源码提交 $commit。通过 SHA256SUMS 检查下载。
EOF

(
  cd "$output"
  gh release create "$tag" "${assets[@]}" SHA256SUMS SOURCE_COMMIT \
    --repo "$repo" --verify-tag --draft --prerelease --latest=false \
    --title "PenChat $version Dev AMR · $short_commit" --notes-file release-note.md
  mkdir download-verification
  gh release download "$tag" --repo "$repo" --dir download-verification
  (cd download-verification && cmp SHA256SUMS ../SHA256SUMS && sha256sum -c SHA256SUMS)
  gh release edit "$tag" --repo "$repo" --draft=false --prerelease --latest=false
  test "$(gh release view "$tag" --repo "$repo" --json isDraft,isPrerelease --jq '.isPrerelease and (.isDraft | not)')" = true
  gh release view "$tag" --repo "$repo" --json url --jq .url
)
