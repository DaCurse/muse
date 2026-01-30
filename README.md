# muse

A minimal Discord bot written in C that detects song links in messages and replies with equivalent links from other music platforms.

The bot communicates directly with the Discord Gateway and REST API, using epoll for event handling and libcurl for WebSockets and HTTP requests.

## References

Using wepoll for Windows:
<https://github.com/piscisaureus/wepoll>

Based on cURL multi event example:
<https://curl.se/libcurl/c/multi-event.html>
