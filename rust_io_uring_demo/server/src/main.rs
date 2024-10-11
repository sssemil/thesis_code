use std::env;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio_uring::net::{TcpListener, TcpStream};
use futures::future::join_all;

const PAGE_SIZE: usize = 4096; 
const PARALLEL_WRITES: usize = 16;

fn main() {
    tokio_uring::start(async {
        let args: Vec<String> = env::args().collect();
        let socket_addr: SocketAddr = if args.len() > 1 {
            args[1].parse().expect("Invalid socket address")
        } else {
            "127.0.0.1:12345".parse().unwrap()
        };

        let listener = TcpListener::bind(socket_addr).expect("Failed to bind");
        println!("Server listening on {}", listener.local_addr().unwrap());

        loop {
            let (stream, peer) = listener
                .accept()
                .await
                .expect("Failed to accept connection");
            println!("Client {} connected", peer);

            tokio_uring::spawn(async move {
                handle_client(stream, peer).await;
            });
        }
    });
}

async fn handle_client(stream: TcpStream, peer: SocketAddr) {
    let mut send_buffer = vec![0u8; PAGE_SIZE];
    for i in 0..PAGE_SIZE {
        send_buffer[i] = (i % 256) as u8;
    }

    let stream = Arc::new(stream);
    let mut tasks = vec![];

    let fixed_buffers: Vec<Vec<u8>> = vec![send_buffer.clone(); PARALLEL_WRITES];

    for i in 0..PARALLEL_WRITES {
        let stream_clone = stream.clone();
        let send_buffer_clone = fixed_buffers[i].clone();
        tasks.push(tokio_uring::spawn(async move {
            loop {
                let (result, _buf) = stream_clone.write(send_buffer_clone.clone()).submit().await;
                match result {
                    Ok(bytes_written) => {
                        if bytes_written == 0 {
                            println!("Client {} disconnected", peer);
                            break;
                        }
                    }
                    Err(e) => {
                        eprintln!("Error sending to {}: {:?}", peer, e);
                        break;
                    }
                }
            }
        }));
    }

    join_all(tasks).await;
}
