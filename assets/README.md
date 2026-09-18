# assets

| Arquivo | Para quê |
|---|---|
| `logo.svg` · `logo-dark.svg` | a marca com o nome, para o topo do README e para quem for citar o projeto |
| `logo-mark.svg` | só a molécula, quadrada — avatar do repositório, ícone, favicon |
| `arquitetura.svg` · `arquitetura-dark.svg` | a visão de arquitetura do README |

## Duas cópias, uma fonte

Os `*.svg` claros são os originais e **trocam de cor sozinhos** por
`prefers-color-scheme` — funcionam em qualquer navegador, inclusive fora daqui.

Os `*-dark.svg` são derivados, e existem só por causa do GitHub: o README usa
`<picture>`, que é o caminho que o GitHub documenta para tema claro e escuro e
que não depende de o sanitizador dele preservar o `<style>` de dentro do SVG.

Editou o original? Regenere:

```sh
python3 assets/gerar-variante-escura.py assets/logo.svg
python3 assets/gerar-variante-escura.py assets/arquitetura.svg
```

## Convenções

- **Sem fonte embutida nem externa.** O texto usa a pilha de fontes do sistema
  (`system-ui`), então o arquivo é pequeno e não depende de rede. Em troca, o
  desenho varia alguns pixels entre sistemas — para um logo de biblioteca, é uma
  troca que vale.
- **Sem raster.** Tudo é vetor, e a marca continua legível a 32 px.
- **Paleta:** azul `#3b82f6` (núcleo), verde-azulado `#14b8a6` (o átomo de
  destaque), ardósia para estrutura e texto. Os equivalentes escuros estão no
  bloco `@media` de cada arquivo.

Para exportar PNG (apresentação, crachá, slide):

```sh
inkscape --export-type=png --export-width=1200 assets/logo.svg
```
