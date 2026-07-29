#!/bin/bash
set -e

SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
ROOT_PATH="$(realpath $SCRIPT_PATH/..)"

# Configuration
DO_CLEAN=false
DO_BUILD=false
DO_BUILD_VERSIONS=false
DO_VIEWHTML=false

print_help()
{
    echo "Usage: $0 [options]"
    echo "  --help                              Display this help message"
    echo "  --clean                             Remove previous build output"
    echo "  --build                             Build the current version of the docs"
    echo "  --build-versions                    Build documentation for all versions"
    echo "  --viewhtml                          Serve the current docs with live reload"
    echo ""
    echo "Examples:"
    echo "  $0 --build                          Build the current version"
    echo "  $0 --build --viewhtml               Build then serve with live reload"
    echo "  $0 --build-versions                 Build all versions"
    echo "  $0 --clean                          Clean all"
    exit 0
}

parse_args()
{
    # Process the arguments
    while [[ "$#" -gt 0 ]]; do
        case $1 in
            --help) print_help;;
            --clean)
                DO_CLEAN=true
                ;;
            --build)
                DO_BUILD=true
                ;;
            --build-versions)
                DO_BUILD_VERSIONS=true
                ;;
            --viewhtml)
                DO_VIEWHTML=true
                ;;
            *) echo "Unknown parameter passed: $1"; exit 1 ;;
        esac
        shift
    done
    return 0
}

build()
{
    echo ">> Building current version..."
    cd ${ROOT_PATH}
    make html
}

build_versions()
{
    echo ">> Building all versions..."
    cd ${ROOT_PATH}
    make multiversion
}

view_html()
{
    cd ${ROOT_PATH}
    make livehtml
}

clean()
{
    echo ">> Cleaning previous build..."
    cd ${ROOT_PATH}
    if [[ -f Makefile ]]; then
        make clean
    fi
}

main()
{
    if [[ "$#" -eq 0 ]]; then
        print_help
    fi

    parse_args "$@"

    if $DO_CLEAN; then
        clean;
    fi

    if $DO_BUILD; then
        build;
    fi

    if $DO_BUILD_VERSIONS; then
        build_versions;
    fi

    if $DO_VIEWHTML; then
        view_html;
    fi

    return 0
}

main "$@"