# Register explicitly selected PPM artifacts for visual review.
mako_register_visual_artifacts() {
    local repo_root=$1
    shift
    local visual_dir="$repo_root/test_output/visual"
    local artifact artifact_name

    mkdir -p "$visual_dir"
    for artifact in "$@"; do
        if [[ ! -f $artifact ]]; then
            printf 'visual artifacts: missing PPM %s\n' "$artifact" >&2
            return 1
        fi
        artifact_name=${artifact##*/}
        ln -sfn "../$artifact_name" "$visual_dir/$artifact_name"
    done
}
