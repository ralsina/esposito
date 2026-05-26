#!/usr/bin/env python3
"""
Fetch top books from Project Gutenberg, download EPUBs, and convert to markdown.
"""

import json
import os
import re
import sys
import subprocess
import urllib.request
from pathlib import Path
from html.parser import HTMLParser


class BookLinksParser(HTMLParser):
    """Parse Project Gutenberg top books page to extract book links."""

    def __init__(self):
        super().__init__()
        self.books = []
        self.in_li = False
        self.in_link = False
        self.current_url = None
        self.current_title = None
        self.found_ol = False
        self.books_collected = 0
        self.max_books = 100  # Only get first 100 books (yesterday's top)

    def handle_starttag(self, tag, attrs):
        if tag == "ol" and not self.found_ol:
            self.found_ol = True
        elif tag == "li" and self.found_ol and self.books_collected < self.max_books:
            self.in_li = True
            self.current_title = None
        elif self.in_li and tag == "a":
            attrs_dict = dict(attrs)
            if "href" in attrs_dict and "/ebooks/" in attrs_dict["href"]:
                self.current_url = attrs_dict["href"]
                self.in_link = True

    def handle_data(self, data):
        if self.in_link:
            self.current_title = data.strip()

    def handle_endtag(self, tag):
        if tag == "a" and self.in_link:
            self.in_link = False
            if self.current_url and self.current_title:
                # Extract book ID from URL
                match = re.search(r"/ebooks/(\d+)", self.current_url)
                if match and self.books_collected < self.max_books:
                    book_id = match.group(1)
                    self.books.append({
                        "id": book_id,
                        "title": self.current_title,
                        "url": self.current_url
                    })
                    self.books_collected += 1
        elif tag == "li":
            self.in_li = False
            self.current_url = None
            self.current_title = None


def sanitize_filename(name):
    """Sanitize a string for use as a filename."""
    # Replace characters that are problematic in filenames
    sanitized = re.sub(r'[<>:"/\\|?*]', '', name)
    # Replace multiple spaces with single space
    sanitized = re.sub(r'\s+', ' ', sanitized)
    # Limit length
    if len(sanitized) > 200:
        sanitized = sanitized[:200]
    return sanitized.strip()


def fetch_top_books(url="https://www.gutenberg.org/browse/scores/top", limit=None):
    """Fetch list of top books from Project Gutenberg."""
    print(f"Fetching top books from {url}...")

    try:
        with urllib.request.urlopen(url) as response:
            html_content = response.read().decode('utf-8')
    except Exception as e:
        print(f"Error fetching page: {e}")
        sys.exit(1)

    parser = BookLinksParser()
    parser.feed(html_content)

    if limit:
        parser.books = parser.books[:limit]

    print(f"Found {len(parser.books)} books")
    return parser.books


def download_epub(book_id, output_dir):
    """Download EPUB file for a book."""
    epub_url = f"https://www.gutenberg.org/ebooks/{book_id}.epub.noimages"
    epub_path = output_dir / f"book_{book_id}.epub"

    print(f"  Downloading EPUB from {epub_url}...")

    try:
        with urllib.request.urlopen(epub_url) as response:
            with open(epub_path, 'wb') as f:
                f.write(response.read())
        print(f"  Saved to {epub_path}")
        return epub_path
    except Exception as e:
        print(f"  Error downloading EPUB: {e}")
        return None


def convert_to_markdown(epub_path, title):
    """Convert EPUB to markdown using pandoc."""
    output_path = epub_path.parent / f"{sanitize_filename(title)}.md"

    print(f"  Converting to markdown: {output_path}")

    try:
        subprocess.run([
            "pandoc",
            str(epub_path),
            "-o", str(output_path),
            "-t", "markdown_strict",
            "--markdown-headings=atx",
            "--strip-comments",
            "--no-highlight"
        ], check=True, capture_output=True)

        print(f"  Converted successfully")
        return output_path
    except subprocess.CalledProcessError as e:
        print(f"  Error converting with pandoc: {e}")
        if e.stderr:
            print(f"  stderr: {e.stderr.decode()}")
        return None
    except FileNotFoundError:
        print(f"  Error: pandoc not found. Please install pandoc.")
        return None


def fetch_book_metadata(book_id):
    """Fetch detailed metadata for a book from its Gutenberg page."""
    metadata_url = f"https://www.gutenberg.org/ebooks/{book_id}"
    print(f"  Fetching metadata from {metadata_url}...")

    try:
        with urllib.request.urlopen(metadata_url) as response:
            html_content = response.read().decode('utf-8')
    except Exception as e:
        print(f"  Error fetching metadata: {e}")
        return None

    metadata = {
        "id": book_id,
        "title": None,
        "author": None,
        "cover_url": None,
        "gutenberg_url": metadata_url
    }

    # Extract title from table row first (cleanest option)
    title_match = re.search(r'<th>Title</th>\s*<td[^>]*>([^<]+)</td>', html_content)
    if not title_match:
        # Fallback to meta tag or title element
        title_match = re.search(r'<meta property="og:title" content="([^"]+)"', html_content)
    if not title_match:
        title_match = re.search(r'<title>\s*(.+?)\s*\|\s*Project Gutenberg', html_content)
    if title_match:
        metadata["title"] = title_match.group(1).strip()

    # Extract author from table row or breadcrumb
    author_match = re.search(r'<th>Author</th>\s*<td>\s*<a[^>]*>([^<]+)</a>', html_content)
    if not author_match:
        author_match = re.search(r'<a href="/ebooks/author/\d+">\d+\s+by\s+([^<]+)</a>', html_content)
    if author_match:
        # Clean up author name (remove dates and extra formatting)
        author = author_match.group(1).strip()
        # Remove dates like "1819-1891"
        author = re.sub(r',\s*\d{4}-\d{4}', '', author)
        # Remove trailing commas and extra spaces
        author = re.sub(r',\s*$', '', author).strip()
        metadata["author"] = author

    # Extract cover image URL from og:image or cover-art class
    cover_match = re.search(r'<meta property="og:image" content="([^"]+)"', html_content)
    if not cover_match:
        cover_match = re.search(r'<img[^>]*class="cover-art"[^>]*src="([^"]+)"', html_content)
    if cover_match:
        metadata["cover_url"] = cover_match.group(1).strip()

    return metadata


def save_metadata_index(all_metadata, output_dir):
    """Save all book metadata to a single JSON index file."""
    metadata_path = output_dir / "metadata.json"

    print(f"\nSaving metadata index to {metadata_path}")

    try:
        with open(metadata_path, 'w', encoding='utf-8') as f:
            json.dump(all_metadata, f, indent=2, ensure_ascii=False)
        print(f"Metadata index saved successfully")
        return metadata_path
    except Exception as e:
        print(f"Error saving metadata index: {e}")
        return None


def process_book(book, output_dir, keep_epub=False):
    """Download and convert a single book, returning metadata with markdown filename."""
    print(f"\nProcessing: {book['title']} (ID: {book['id']})")

    # Fetch detailed metadata
    metadata = fetch_book_metadata(book['id'])
    if not metadata:
        # Fallback to basic info if metadata fetch fails
        metadata = {
            "id": book['id'],
            "title": book['title'],
            "author": None,
            "cover_url": None,
            "gutenberg_url": f"https://www.gutenberg.org{book['url']}"
        }

    # Download EPUB
    epub_path = download_epub(book['id'], output_dir)
    if not epub_path:
        return None

    # Convert to markdown
    md_path = convert_to_markdown(epub_path, metadata['title'] or book['title'])
    if not md_path:
        return None

    # Add markdown filename to metadata
    metadata["markdown_file"] = md_path.name

    # Clean up EPUB if requested
    if not keep_epub:
        epub_path.unlink()
        print(f"  Removed EPUB file")

    return metadata


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Fetch and convert books from Project Gutenberg (top 100 from yesterday)"
    )
    parser.add_argument(
        "--output-dir", "-o",
        default="books",
        help="Output directory for downloaded books (default: books)"
    )
    parser.add_argument(
        "--limit", "-n",
        type=int,
        default=None,
        help="Limit number of books to download"
    )
    parser.add_argument(
        "--keep-epub",
        action="store_true",
        help="Keep EPUB files after conversion"
    )
    parser.add_argument(
        "--book-id",
        type=int,
        help="Download specific book by ID instead of top books list"
    )
    parser.add_argument(
        "--url",
        default="https://www.gutenberg.org/browse/scores/top",
        help="Project Gutenberg URL to fetch books from"
    )

    args = parser.parse_args()

    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(exist_ok=True)

    if args.book_id:
        # Download single book
        print(f"Fetching book {args.book_id}...")
        book = {
            "id": str(args.book_id),
            "title": f"book_{args.book_id}",
            "url": f"/ebooks/{args.book_id}"
        }
        books = [book]
    else:
        # Fetch top books list
        books = fetch_top_books(args.url, args.limit)

    if not books:
        print("No books found")
        sys.exit(1)

    # Process each book and collect metadata
    success_count = 0
    all_metadata = []

    for book in books:
        metadata = process_book(book, output_dir, args.keep_epub)
        if metadata:
            all_metadata.append(metadata)
            success_count += 1

    # Save metadata index
    if all_metadata:
        save_metadata_index(all_metadata, output_dir)

    print(f"\n{'='*60}")
    print(f"Successfully processed {success_count}/{len(books)} books")
    print(f"Files saved to: {output_dir.absolute()}")


if __name__ == "__main__":
    main()
