# C-Proxy: A Multithreaded Caching Proxy Server

This project is a high-performance, multithreaded caching web proxy server developed in C on a Linux platform. It is designed to be an efficient intermediary between web clients and servers, accelerating web access by caching frequently requested content and handling a large number of concurrent connections gracefully.

### Introduction

My name is Aditya Negi, and I am a passionate software developer with a keen interest in systems programming, networking, and high-performance computing. I developed this project to deepen my understanding of complex, low-level concepts such as multithreading, memory management, and TCP/IP networking. This endeavor allowed me to tackle real-world engineering challenges, from designing a scalable concurrency model to debugging intricate parsing logic, ultimately creating a robust and feature-rich application entirely in C.

---

## Project Overview & Architecture

The server acts as an intermediary for web requests. Instead of clients connecting directly to a web server, they connect to this proxy. The proxy then fetches the requested content, forwards it to the client, and intelligently caches it to speed up subsequent requests.



### Core Architectural Components

* **TCP Networking Core:** The foundation of the server is built on the standard Linux TCP socket API (`socket`, `bind`, `listen`, `accept`). It establishes a listening socket and manages client connections reliably.

* **Concurrency Model: Thread Pool:** To handle a high volume of simultaneous clients, the server uses a **thread pool** (Producer-Consumer model).
    * **Main Thread (Producer):** Its sole responsibility is to accept new client connections and place their socket descriptors into a thread-safe task queue.
    * **Worker Threads (Consumers):** A fixed number of worker threads are created at startup. They wait on a condition variable until a task appears in the queue. Each thread processes one client request from start to finish and then returns to the pool to await another task. This model avoids the high overhead of creating a new thread for every request, making the server highly scalable.

* **High-Performance LRU Cache:** To minimize latency, the proxy features a custom-built, thread-safe Least Recently Used (LRU) cache.
    * **Data Structures:** It employs a classic and highly efficient design combining a **Hash Table** and a **Doubly-Linked List**. This provides **O(1)** average time complexity for all core operations (add, get, evict).
    * **Functionality:** When a request is made for cachable content (HTTP GET), the server first checks the cache. A **cache hit** results in an immediate response from memory. A **cache miss** triggers a request to the origin server, and the response is then stored in the cache for future access, evicting the least recently used item if the cache is full.

---

## Key Features

This project evolved from a basic server to a robust application with a suite of advanced features:

* **HTTPS Support (CONNECT Method):** The proxy can handle secure HTTPS traffic. It correctly processes the `CONNECT` method, establishing a TCP tunnel between the client and the destination server to shuttle encrypted data back and forth without inspection.

* **Configuration File (`proxy.conf`):** Server settings are externalized into a simple configuration file. This allows an administrator to easily change the **port**, **thread pool size**, and **cache capacity** without recompiling the source code.

* **Robust Logging (`proxy.log`):** All server activity is logged to a file with timestamps and severity levels (e.g., `[INFO]`, `[WARN]`, `[ERROR]`). The logging mechanism is thread-safe, ensuring that messages from concurrent threads do not get interleaved.

* **Domain Filtering (`blacklist.txt`):** The server can block access to specified domains. It reads a list of domains from a `blacklist.txt` file at startup and will return an `HTTP 403 Forbidden` error if a client requests a blacklisted host.

---

## Project Timeline & Development

This project was developed iteratively over a period of intensive work.

* **Initial Conception (August 8, 2025):** The project began with the foundational goal of building a simple multithreaded server and a basic linked-list cache.
* **Core Implementation (August 9, 2025):** The initial, functional version was completed, featuring the thread pool architecture and a working (though inefficient) LRU cache.
* **Performance & Feature Enhancements (August 9-10, 2025):** The simple cache was replaced with the high-performance hash table and doubly-linked list implementation. Advanced features including HTTPS tunneling, the configuration file, logging, and the blacklist were integrated.
* **Debugging & Stabilization (August 10, 2025):** This phase involved intensive debugging, particularly focusing on the complex HTTP parsing library, process management in the terminal, and ensuring end-to-end functionality.

---

## Challenges Faced & Solutions

This project presented several significant technical challenges, each providing a valuable learning experience.

* **The Parser Bug:** The most persistent challenge was debugging the open-source `proxy_parse.c` library.
    * **Problem:** The original parsing logic was flawed, failing to correctly tokenize HTTP request lines. This resulted in a cascade of errors, including "Failed to parse request" and "Cannot resolve hostname," as the `host` field was often left empty.
    * **Solution:** After extensive debugging—which involved adding print statements to view the raw buffer being processed—I completely rewrote the core `ParsedRequest_parse` function. The new implementation uses a more robust series of `strtok` and `strchr` calls to correctly and reliably extract the method, URI, host, port, and path for both `GET` and `CONNECT` requests.

* **Process Management:** During testing, the server would often fail to start with an "Address already in use" error.
    * **Problem:** I discovered I was accidentally suspending the server process (`Ctrl+Z`) instead of terminating it (`Ctrl+C`). This left a "zombie" process holding the port open in the background.
    * **Solution:** I learned to use command-line tools like `jobs`, `fg`, `lsof`, and `kill -9` to effectively manage background processes and free up occupied ports, a crucial skill for systems development on Linux.

* **Request Forwarding Logic:** An early bug caused the `test_client` to receive empty responses.
    * **Problem:** The proxy was forwarding the client's proxy-style request (`GET http://example.com HTTP/1.0`) directly to the origin server. However, origin servers expect a server-style request with a relative path (`GET / HTTP/1.0`).
    * **Solution:** I implemented logic in the `handle_http_request` function to re-write the request line into the correct format before forwarding it, ensuring compliance with HTTP standards.

---

## How to Compile and Run

**1. Compile the Project**
   Use the provided `Makefile` to build both the server and the test client.
   ```bash
   make
````

**2. Configure the Server**
Modify the `proxy.conf` and `blacklist.txt` files as needed.

  * **`proxy.conf`**: Set the `port`, `threads`, and `cache_size_mb`.
  * **`blacklist.txt`**: Add any domains you wish to block (one per line, e.g., `example.com`).

**3. Run the Server**

```bash
./proxy_server
```

The server will run in the foreground. All activity will be logged to `proxy.log`.

**4. Run the Test Client (Optional)**
Open a new terminal to send a test request.

```bash
./test_client localhost 8888 [http://example.com](http://example.com)
```

**5. Configure Your Browser**
To use the proxy with your browser, manually configure its network settings:

  * **Host/Address:** `127.0.0.1`
  * **Port:** `8888` (or your configured port)
  * Ensure you check the option **"Also use this proxy for HTTPS"**.

-----

## Credits and References

This project stands on the shoulders of excellent educational resources and my own significant modifications and enhancements.

  * **Project Core & Enhancements:** The core server architecture, thread pool, high-performance LRU cache, and the integration of all advanced features (HTTPS support, logging, configuration file, filtering) were designed and implemented by me, **Cyrus**.

  * **Guidance and Collaboration:** This project was developed in collaboration with **Google's Gemini**, which provided high-level architectural guidance, feature implementation strategies, and extensive debugging support.

  * **Foundational Concepts Reference:** The YouTube video "[Building a Multithreaded Web Server in C](https://www.youtube.com/watch?v=eTvSgOoc_BE)" by **CodeVault** served as an excellent foundational reference for understanding the basic principles of a multithreaded server in C.

      * **My Modifications and Enhancements:** While the video provided a great starting point, this project significantly expands upon its concepts. I implemented a more advanced and efficient **thread pool model** instead of a thread-per-request architecture. Furthermore, I designed and built a custom, high-performance **LRU cache** using a hash table and doubly-linked list, a feature not present in the reference material. I also independently added all other advanced features like **HTTPS tunneling**, **external configuration**, and **domain blacklisting**.

  * **Parsing Library:** The initial versions of `proxy_parse.h` and `proxy_parse.c` were based on an open-source library provided for educational purposes by Princeton University's COS 461 course. The final, working version of `proxy_parse.c` in this repository is a heavily modified and corrected implementation created to fix significant parsing bugs in the original.

<!-- end list -->

