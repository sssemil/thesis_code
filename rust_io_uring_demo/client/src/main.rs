use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::Mutex;
use tokio_uring::net::TcpStream;
use futures::future::join_all;

const PAGE_SIZE: usize = 4096; 
const DURATION_SECS: u64 = 10;
const PARALLEL_READS: usize = 16;

#[derive(Default)]
struct Stats {
    total_bytes: usize,
    total_pages: usize,
}

fn main() {
    tokio_uring::start(async {
        let args: Vec<String> = std::env::args().collect();
        let server_addr: SocketAddr = if args.len() > 1 {
            args[1].parse().expect("Invalid server address")
        } else {
            "127.0.0.1:12345".parse().unwrap()
        };

        let stream = TcpStream::connect(server_addr)
            .await
            .expect("Failed to connect to server");
        println!("Connected to server at {}", server_addr);

        let stream = Arc::new(stream);
        let stats = Arc::new(Mutex::new(Stats::default()));
        let start_time = Instant::now();
        let end_time = start_time + Duration::from_secs(DURATION_SECS);

        let fixed_buffers: Vec<Vec<u8>> = vec![vec![0u8; PAGE_SIZE]; PARALLEL_READS];

        let mut tasks = vec![];
        for i in 0..PARALLEL_READS {
            let stream_clone = stream.clone();
            let stats_clone = stats.clone();
            let mut buffer = fixed_buffers[i].clone();
            tasks.push(tokio_uring::spawn(async move {
                loop {
                    if Instant::now() >= end_time {
                        break;
                    }
                    let (result, returned_buffer) = stream_clone.read(buffer).await;
                    buffer = returned_buffer;

                    match result {
                        Ok(bytes_read) => {
                            if bytes_read == 0 {
                                println!("Server closed the connection");
                                break;
                            }
                            let mut stats = stats_clone.lock().await;
                            stats.total_bytes += bytes_read;
                            stats.total_pages += 1;
                        }
                        Err(e) => {
                            eprintln!("Error reading from server: {:?}", e);
                            break;
                        }
                    }
                }
            }));
        }

        join_all(tasks).await;

        let stats = stats.lock().await;
        let elapsed_secs = start_time.elapsed().as_secs_f64();
        let throughput_gbps = (stats.total_bytes as f64 * 8.0) / (elapsed_secs * 1e9);
        let pages_per_sec = stats.total_pages as f64 / elapsed_secs;

        println!(
            "Received {} bytes in {:.2} seconds",
            stats.total_bytes, elapsed_secs
        );
        println!("Throughput: {:.6} Gbps", throughput_gbps);
        println!("Pages per second: {:.2}", pages_per_sec);
    });
}
