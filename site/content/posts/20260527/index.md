---
title: ESP-Osito News for May 27, 2026
date: 2026-05-27 17:01:15 -03:00
---

## The ebook reader now has a bookstore

You can go to <https://esposito.ralsina.me/books.html> and pick any of 100
books. Plug your device via USB, open the ebook reader, click "get" in the book
list, then click the download icon on the book. The book automatically downloads to the device!

This is mostly intended as a testbed for delivering "stuff" to the devices, like an app store
or anything else. But in the meantime, hey, books to read!

{{% tag div class="grid" %}}
{{% card %}}
{{% figure src="/reader-download.png" caption="Downloading from the bookshop!" link="https://github.com/ralsina/esposito/tree/main/apps/reader" %}}
{{% /card %}}
{{% card %}}
{{% figure src="https://cdn.bsky.app/img/feed_fullsize/plain/did:plc:735jqbf5vjpoc5a6w6kqkp2d/bafkreiczen3yg6rbb4u2szpkfha5xmgtge7zgykhnu53w7joe3fvafrc7e" caption="DOZENS of books" link="https://github.com/ralsina/esposito/tree/main/apps/reader" %}}
{{% /card %}}
{{% /tag %}}

## Better Unicode Support

Previously most things were ASCII only. Now full unicode support is there. The
limitation is that a full unicode font would not *fit* so I added all the normal
latin characters, plus some symbols we can use for better UI, so we can show "▼"
rather than write "DOWN" in a button.

Also, of course, this makes the ebook reader look a lot better with better quote
characters, en or em-dash, accented characters and so on.
