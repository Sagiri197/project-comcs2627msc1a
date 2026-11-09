#!/usr/bin/env bash
# Render every docs/diagrams/*.puml file to docs/images/.
#
# Usage: ./.scripts/gen_puml_diagrams.sh
#
# Uses the `plantuml` command when it is on PATH; otherwise runs
# `java -jar "$PLANTUML_JAR"` (default: C:/Tools/PlantUML/plantuml.jar).
set -euo pipefail

cd "$(dirname "$0")/../docs/diagrams"

if command -v plantuml >/dev/null 2>&1; then
    plantuml_cmd=(plantuml)
else
    jar="${PLANTUML_JAR:-C:/Tools/PlantUML/plantuml.jar}"
    if [[ ! -f "$jar" ]]; then
        echo "error: plantuml not on PATH and jar not found at '$jar'." >&2
        echo "       Install PlantUML or set PLANTUML_JAR." >&2
        exit 1
    fi
    plantuml_cmd=(java -jar "$jar")
fi

mapfile -t files < <(find . -name '*.puml' -type f | sort)

if [[ ${#files[@]} -eq 0 ]]; then
    echo "No .puml files found."
    exit 0
fi

for f in "${files[@]}"; do
    echo "Rendering $f"
done

"${plantuml_cmd[@]}" -tpng -failfast2 -o ../images "${files[@]}"

echo "Done: ${#files[@]} diagram(s) rendered."
