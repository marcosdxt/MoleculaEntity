#!/usr/bin/env python3
"""Gera o `-dark.svg` a partir do SVG claro, promovendo o bloco @media.

Por que existe: os SVGs daqui já trocam de cor sozinhos pelo
`prefers-color-scheme`, e isso vale em qualquer navegador. O README, porém, usa
`<picture>` com dois arquivos — que é o caminho que o GitHub documenta e que não
depende de o sanitizador dele preservar o `<style>` de dentro do SVG.

    python3 assets/gerar-variante-escura.py assets/logo.svg

Rode depois de editar o original; a variante é derivada e não se edita à mão.
"""
import sys
from pathlib import Path

MARCA = "@media (prefers-color-scheme: dark) {"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    origem = Path(sys.argv[1])
    svg = origem.read_text()

    if MARCA not in svg:
        print(f"{origem}: não tem bloco @media para promover")
        return 1

    inicio = svg.index(MARCA)
    fim_estilo = svg.index("</style>")
    bloco = svg[inicio:fim_estilo]
    regras = bloco[bloco.index("{") + 1:bloco.rindex("}")]

    destino = origem.with_name(origem.stem + "-dark.svg")
    destino.write_text(
        svg[:inicio]
        + "    /* GERADO por assets/gerar-variante-escura.py — não edite. */\n"
        + regras
        + svg[fim_estilo:]
    )
    print(f"{destino}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
