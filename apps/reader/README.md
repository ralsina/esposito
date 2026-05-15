# Esposito Ebook Reader

A markdown ebook reader for the Esposito terminal OS on ESP32/CYD2USB.

![Reader in Action](reader.png)

## Features

* Reads `.md` files from `/sdcard/books/` and displays them with paragraph reflow, page navigation, and checkpoint resume.
* Opens instantly, pages flip instantly
* Remembers open book/position

## Controls

| Key | Action |
| --- | --- |
| `W` | Previous page |
| `S` | Next page |
| `G` | Go to page (enter number, Enter to jump, ESC to cancel) |
| `/` | Search forward from current position |
| `ESC` | Return to book list |

## Converting books

Use pandoc to convert EPUB/other formats to the markdown this reader supports:

```sh
pandoc ~/Downloads/mybook.epub -o mybook.md \
  -t markdown_strict \
  --markdown-headings=atx \
  --strip-comments \
  --no-highlight
```

Copy the resulting `.md` file to the `books` folder on the SD card:

```sh
cp mybook.md /path/to/sdcard/books/
```

## Details

* Paragraph joining: consecutive non-blank lines are reflowed as a single paragraph
* Heading support: H1 in bright white+bold, H2+ in cyan+bold
* Horizontal rules: `----` (4+ dashes) rendered as a full-width line
* HTML tag stripping: inline and multi-line tags are removed automatically
* Unicode→ASCII conversion: curly quotes, em dashes, ellipsis mapped to ASCII
* Mid-paragraph page breaks with seamless continuation
* Page cache: 16-entry ring buffer for forward/backward navigation
* Checkpoint: saves current file and position, resumes on next launch
* Top bar: filename + page number with navigation hints
