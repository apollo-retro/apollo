use apollo_hyper_libretro_build::build;

fn main() {
    build("beetle-wswan-libretro", Some("mednafen_wswan.patch"), None);
}
