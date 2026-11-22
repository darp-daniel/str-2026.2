// mini_teste.hpp — framework mínimo de testes (sem dependências externas).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mini {
struct Caso {
  const char *nome;
  std::function<void()> f;
};
inline std::vector<Caso> &casos() {
  static std::vector<Caso> v;
  return v;
}
inline int &falhas() {
  static int n = 0;
  return n;
}
struct Registro {
  Registro(const char *n, std::function<void()> f) { casos().push_back({n, f}); }
};
inline int rodar() {
  int ok = 0;
  for (auto &c : casos()) {
    int antes = falhas();
    c.f();
    if (falhas() == antes) {
      ++ok;
      std::printf("  ok    %s\n", c.nome);
    } else {
      std::printf("  FALHA %s\n", c.nome);
    }
  }
  std::printf("%d/%zu casos ok\n", ok, casos().size());
  return falhas() == 0 ? 0 : 1;
}
} // namespace mini

#define TESTE(nome)                                                                                                    \
  static void nome();                                                                                                  \
  static mini::Registro reg_##nome(#nome, nome);                                                                       \
  static void nome()

#define CHECA(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      ++mini::falhas();                                                                                                \
      std::printf("    %s:%d: falhou: %s\n", __FILE__, __LINE__, #c);                                                  \
    }                                                                                                                  \
  } while (0)

#define CHECA_PERTO(a, b, tol)                                                                                         \
  do {                                                                                                                 \
    double va_ = (a), vb_ = (b);                                                                                       \
    if (std::fabs(va_ - vb_) > (tol)) {                                                                                \
      ++mini::falhas();                                                                                                \
      std::printf("    %s:%d: %s = %g, esperado %g (+-%g)\n", __FILE__, __LINE__, #a, va_, vb_, (double)(tol));       \
    }                                                                                                                  \
  } while (0)

#define MINI_MAIN                                                                                                      \
  int main() { return mini::rodar(); }
