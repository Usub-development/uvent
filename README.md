# Uvent

### Requests per second (RPS)

| Threads | uvent       | Boost.Asio  | libuv   |
|---------|-------------|-------------|---------|
| 1       | 88,929      | 97,219      |    116  |
| 2       | 172,986     | 185,813     |    828  |
| 4       | 298,269     | 330,374     |    830  |
| 8       | 409,388     | 423,409     |    827  |

⚡ **Conclusion:** `uvent` delivers performance nearly on par with Boost.Asio and significantly outperforms libuv, while keeping low latency (p99 around 2–3 ms).

👉 For more detailed and up-to-date benchmark results, see the dedicated repository: [Usub-development/io_perfomance](https://github.com/Usub-development/io_perfomance)
