use std::env;
use std::io;
use std::net::{IpAddr, Ipv4Addr, SocketAddr, SocketAddrV4, TcpStream};
use std::time::Duration;

use socket2::{Domain, Protocol, Socket, Type};

const DEFAULT_TARGET: &str = "203.0.113.179:7447";
const DEFAULT_ETHERNET: &str = "203.0.113.201";

fn target() -> SocketAddrV4 {
    env::var("ZENOH_DIAG_TARGET")
        .unwrap_or_else(|_| DEFAULT_TARGET.to_string())
        .parse()
        .expect("ZENOH_DIAG_TARGET must be an IPv4 socket address")
}

fn bind_address(value: &str) -> SocketAddrV4 {
    SocketAddrV4::new(value.parse().expect("bind address must be IPv4"), 0)
}

fn show_result(label: &str, result: io::Result<TcpStream>) {
    match result {
        Ok(stream) => println!("{label}: OK local={} peer={}", stream.local_addr().unwrap(), stream.peer_addr().unwrap()),
        Err(error) => println!("{label}: ERROR kind={:?} raw_os_error={:?} message={error}", error.kind(), error.raw_os_error()),
    }
}

fn std_connect(target: SocketAddrV4) {
    show_result("std::net unbound", TcpStream::connect_timeout(&target.into(), Duration::from_secs(3)));
}

fn socket2_connect(label: &str, target: SocketAddrV4, bind: Option<SocketAddrV4>) {
    let result = (|| -> io::Result<TcpStream> {
        let socket = Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP))?;
        socket.set_nonblocking(false)?;
        if let Some(bind) = bind {
            socket.bind(&bind.into())?;
        }
        socket.connect_timeout(&target.into(), Duration::from_secs(3))?;
        Ok(socket.into())
    })();
    show_result(label, result);
}

async fn tokio_connect(label: &str, target: SocketAddrV4, bind: Option<SocketAddrV4>) {
    let result = async {
        let socket = tokio::net::TcpSocket::new_v4()?;
        if let Some(bind) = bind {
            socket.bind(bind.into())?;
        }
        socket.connect(SocketAddr::V4(target)).await
    };
    match tokio::time::timeout(Duration::from_secs(3), result).await {
        Ok(result) => match result {
            Ok(stream) => println!("{label}: OK local={} peer={}", stream.local_addr().unwrap(), stream.peer_addr().unwrap()),
            Err(error) => println!("{label}: ERROR kind={:?} raw_os_error={:?} message={error}", error.kind(), error.raw_os_error()),
        },
        Err(_) => println!("{label}: ERROR timeout after 3s"),
    }
}

#[tokio::main]
async fn main() {
    let target = target();
    let ethernet = env::var("ZENOH_DIAG_ETHERNET_IPV4").unwrap_or_else(|_| DEFAULT_ETHERNET.to_string());
    let wifi = env::var("ZENOH_DIAG_WIFI_IPV4").unwrap_or_else(|_| "198.51.100.134".to_string());
    let any = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);
    println!("target={target} family=AF_INET ethernet={ethernet} wifi={wifi}");
    std_connect(target);
    socket2_connect("socket2 unbound", target, None);
    socket2_connect("socket2 bind ethernet", target, Some(bind_address(&ethernet)));
    socket2_connect("socket2 bind wifi", target, Some(bind_address(&wifi)));
    socket2_connect("socket2 bind any", target, Some(any));
    tokio_connect("tokio unbound", target, None).await;
    tokio_connect("tokio bind ethernet", target, Some(bind_address(&ethernet))).await;
    tokio_connect("tokio bind wifi", target, Some(bind_address(&wifi))).await;
    tokio_connect("tokio bind any", target, Some(any)).await;
    let _ = IpAddr::V4(*target.ip());
}
