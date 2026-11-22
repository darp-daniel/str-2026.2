#include "enlace.hpp"

#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace str_sim {

// ======================================================== EnlaceVirtual
void EnlaceVirtual::enviar(const SimParaEsp &m, uint64_t t_us) {
  esp_.receber(m, t_us);
  esp_.servico(t_us);
  est_.quadros_enviados++;
}

bool EnlaceVirtual::receber(uint64_t t_us, EspParaSim &out) {
  esp_.servico(t_us);
  if (!esp_.proximaResposta(t_us, out)) return false;
  est_.quadros_recebidos++;
  return true;
}

// ======================================================== EnlaceBytes
void EnlaceBytes::enviar(const SimParaEsp &m, uint64_t) {
  uint8_t q[SIMP_MAX_QUADRO];
  size_t n = simp_montar(SIMP_TIPO_SIM_PARA_ESP, &m, sizeof(m), q);
  if (escreverBytes(q, n)) est_.quadros_enviados++;
}

void EnlaceBytes::alimentarTexto(uint8_t b) {
  est_.bytes_texto++;
  if (b == '\n' || linha_.size() > 300) {
    while (!linha_.empty() && (linha_.back() == '\r' || linha_.back() == ' ')) linha_.pop_back();
    if (!linha_.empty() && aoTexto) aoTexto(linha_);
    linha_.clear();
  } else if (b >= 0x20 || b == '\t') {
    linha_.push_back(static_cast<char>(b));
  }
}

bool EnlaceBytes::receber(uint64_t, EspParaSim &out) {
  for (;;) {
    if (buf_i_ >= buf_n_) {
      buf_n_ = lerBytes(buf_, sizeof(buf_));
      buf_i_ = 0;
      if (buf_n_ == 0) return false;
    }
    while (buf_i_ < buf_n_) {
      uint8_t b = buf_[buf_i_++];
      const bool fora_de_quadro = dec_.estado == 0 && b != SIMP_SYNC0;
      uint8_t tipo = simp_alimentar(&dec_, b);
      if (fora_de_quadro) alimentarTexto(b);
      est_.erros_crc = dec_.erros_crc;
      if (tipo == SIMP_TIPO_ESP_PARA_SIM && dec_.len == sizeof(EspParaSim)) {
        std::memcpy(&out, dec_.payload, sizeof(out));
        est_.quadros_recebidos++;
        return true;
      }
    }
  }
}

// ======================================================== SocketUdp
#ifdef _WIN32
static void iniciarWinsock() {
  static bool ok = [] {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }();
  if (!ok) throw std::runtime_error("WSAStartup falhou");
}
using socklen_compat = int;
static void fecharSocket(intptr_t s) { closesocket(static_cast<SOCKET>(s)); }
#else
static void iniciarWinsock() {}
using socklen_compat = socklen_t;
static void fecharSocket(intptr_t s) { close(static_cast<int>(s)); }
#endif

SocketUdp::SocketUdp(uint16_t porta_local) {
  iniciarWinsock();
  static_assert(sizeof(destino_) >= sizeof(sockaddr_in), "destino_");
  auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if (s == INVALID_SOCKET) throw std::runtime_error("socket UDP falhou");
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  if (s < 0) throw std::runtime_error("socket UDP falhou");
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  s_ = static_cast<intptr_t>(s);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(porta_local);
  if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
    fecharSocket(s_);
    throw std::runtime_error("nao consegui usar a porta UDP " + std::to_string(porta_local));
  }
  socklen_compat len = sizeof(a);
  getsockname(s, reinterpret_cast<sockaddr *>(&a), &len);
  porta_local_ = ntohs(a.sin_port);
}

SocketUdp::~SocketUdp() {
  if (s_ != -1) fecharSocket(s_);
}

void SocketUdp::definirDestino(const std::string &host, uint16_t porta) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(porta);
  const std::string h = (host == "localhost") ? "127.0.0.1" : host;
  if (inet_pton(AF_INET, h.c_str(), &a.sin_addr) != 1)
    throw std::runtime_error("endereco IPv4 invalido: " + host);
  std::memcpy(destino_, &a, sizeof(a));
  tem_destino_ = true;
}

bool SocketUdp::enviar(const uint8_t *b, size_t n) {
  if (!tem_destino_) return false;
  auto r = sendto(static_cast<decltype(socket(0, 0, 0))>(s_), reinterpret_cast<const char *>(b),
                  static_cast<int>(n), 0, reinterpret_cast<const sockaddr *>(destino_), sizeof(sockaddr_in));
  return r == static_cast<decltype(r)>(n);
}

size_t SocketUdp::receber(uint8_t *b, size_t max, bool responderAoRemetente) {
  sockaddr_in de{};
  socklen_compat len = sizeof(de);
  auto r = recvfrom(static_cast<decltype(socket(0, 0, 0))>(s_), reinterpret_cast<char *>(b),
                    static_cast<int>(max), 0, reinterpret_cast<sockaddr *>(&de), &len);
  if (r <= 0) return 0; // sem dados (ou ICMP port unreachable no Windows)
  if (responderAoRemetente) {
    std::memcpy(destino_, &de, sizeof(de));
    tem_destino_ = true;
  }
  return static_cast<size_t>(r);
}

size_t SocketUdp::receberComTimeout(uint8_t *b, size_t max, int timeout_ms) {
#ifdef _WIN32
  fd_set fs;
  FD_ZERO(&fs);
  FD_SET(static_cast<SOCKET>(s_), &fs);
  timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  if (select(0, &fs, nullptr, nullptr, &tv) <= 0) return 0;
#else
  pollfd p{static_cast<int>(s_), POLLIN, 0};
  if (poll(&p, 1, timeout_ms) <= 0) return 0;
#endif
  return receber(b, max, false);
}

// ======================================================== EnlaceUdp
EnlaceUdp::EnlaceUdp(const std::string &host, uint16_t porta_remota, uint16_t porta_local)
    : sock_(porta_local) {
  sock_.definirDestino(host, porta_remota);
  desc_ = "UDP " + host + ":" + std::to_string(porta_remota) + " (local " +
          std::to_string(sock_.portaLocal()) + ")";
}

bool EnlaceUdp::escreverBytes(const uint8_t *b, size_t n) { return sock_.enviar(b, n); }
size_t EnlaceUdp::lerBytes(uint8_t *b, size_t max) { return sock_.receber(b, max); }

// ======================================================== EnlaceSerial
#ifdef _WIN32
EnlaceSerial::EnlaceSerial(const std::string &porta, int baud) {
  std::string nome = porta.rfind("\\\\.\\", 0) == 0 ? porta : "\\\\.\\" + porta;
  HANDLE h = CreateFileA(nome.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    throw std::runtime_error("nao consegui abrir " + porta +
                             " (porta certa? o 'idf.py monitor' esta fechado?)");
  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);
  GetCommState(h, &dcb);
  dcb.BaudRate = static_cast<DWORD>(baud);
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  // DTR/RTS desligados: nas placas DevKit eles ligam no EN/IO0 e podem
  // resetar a ESP32 ou prendê-la no bootloader.
  dcb.fDtrControl = DTR_CONTROL_DISABLE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  if (!SetCommState(h, &dcb)) {
    CloseHandle(h);
    throw std::runtime_error("SetCommState falhou em " + porta);
  }
  COMMTIMEOUTS to{};
  to.ReadIntervalTimeout = MAXDWORD; // leitura retorna na hora
  to.ReadTotalTimeoutConstant = 0;
  to.ReadTotalTimeoutMultiplier = 0;
  to.WriteTotalTimeoutConstant = 50;
  SetCommTimeouts(h, &to);
  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
  h_ = reinterpret_cast<intptr_t>(h);
  desc_ = "Serial " + porta + " @ " + std::to_string(baud);
}

EnlaceSerial::~EnlaceSerial() {
  if (h_ != -1) CloseHandle(reinterpret_cast<HANDLE>(h_));
}

bool EnlaceSerial::escreverBytes(const uint8_t *b, size_t n) {
  DWORD w = 0;
  return WriteFile(reinterpret_cast<HANDLE>(h_), b, static_cast<DWORD>(n), &w, nullptr) && w == n;
}

size_t EnlaceSerial::lerBytes(uint8_t *b, size_t max) {
  DWORD r = 0;
  if (!ReadFile(reinterpret_cast<HANDLE>(h_), b, static_cast<DWORD>(max), &r, nullptr)) return 0;
  return r;
}
#else
static speed_t velocidadePosix(int baud) {
  switch (baud) {
  case 9600: return B9600;
  case 57600: return B57600;
  case 115200: return B115200;
  case 230400: return B230400;
#ifdef B460800
  case 460800: return B460800;
#endif
#ifdef B921600
  case 921600: return B921600;
#endif
  default: throw std::runtime_error("baud nao suportado: " + std::to_string(baud));
  }
}

EnlaceSerial::EnlaceSerial(const std::string &porta, int baud) {
  int fd = open(porta.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) throw std::runtime_error("nao consegui abrir " + porta);
  termios t{};
  tcgetattr(fd, &t);
  cfmakeraw(&t);
  cfsetispeed(&t, velocidadePosix(baud));
  cfsetospeed(&t, velocidadePosix(baud));
  t.c_cflag |= CLOCAL | CREAD;
  t.c_cflag &= ~CRTSCTS;
  tcsetattr(fd, TCSANOW, &t);
  tcflush(fd, TCIOFLUSH);
  h_ = fd;
  desc_ = "Serial " + porta + " @ " + std::to_string(baud);
}

EnlaceSerial::~EnlaceSerial() {
  if (h_ != -1) close(static_cast<int>(h_));
}

bool EnlaceSerial::escreverBytes(const uint8_t *b, size_t n) {
  return write(static_cast<int>(h_), b, n) == static_cast<ssize_t>(n);
}

size_t EnlaceSerial::lerBytes(uint8_t *b, size_t max) {
  ssize_t r = read(static_cast<int>(h_), b, max);
  return r > 0 ? static_cast<size_t>(r) : 0;
}
#endif

// ======================================================== fábrica
std::unique_ptr<Enlace> criarEnlace(const std::string &spec, int baud, EspVirtual *esp_virtual) {
  if (spec == "virtual") {
    if (!esp_virtual) throw std::runtime_error("ESP virtual nao fornecida");
    return std::make_unique<EnlaceVirtual>(*esp_virtual);
  }
  if (spec.rfind("udp:", 0) == 0) {
    auto resto = spec.substr(4);
    auto dp = resto.rfind(':');
    if (dp == std::string::npos) throw std::runtime_error("use udp:host:porta");
    return std::make_unique<EnlaceUdp>(resto.substr(0, dp),
                                       static_cast<uint16_t>(std::stoi(resto.substr(dp + 1))));
  }
  if (spec.rfind("serial:", 0) == 0) return std::make_unique<EnlaceSerial>(spec.substr(7), baud);
  throw std::runtime_error("enlace desconhecido: " + spec + " (use virtual, udp:host:porta ou serial:PORTA)");
}

} // namespace str_sim
