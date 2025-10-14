#!/usr/bin/env bash
set -euo pipefail

# === Configuração ===
SERV_BIN="./servidor"
CLI_BIN="./cliente"

PORT="${1:-8080}"          # porta do servidor (padrão: 8080)  -> pode passar como 1º arg
TEMPO_SLEEP="${2:-5}"      # TEMPO_SLEEP para o servidor       -> pode passar como 2º arg
CLIENTES_POR_RODADA=10     # quantidade de clientes simultâneos
BACKLOG_INICIAL=0
BACKLOG_FINAL=10           # inclusive
PAUSA_APOS_START=1       # segundos para o servidor iniciar
PAUSA_POS_CLIENTES=0.1     # aguardar estabilizar ESTABLISHED após disparar clientes
OUT_CSV="resultados_backlog.csv"

# === Funções de utilidade ===
tcp_max_syn_backlog() {
  if [[ -r /proc/sys/net/ipv4/tcp_max_syn_backlog ]]; then
    cat /proc/sys/net/ipv4/tcp_max_syn_backlog
  else
    echo "desconhecido"
  fi
}

tem_cmd() { command -v "$1" >/dev/null 2>&1; }

conta_established() {
  local port="$1"
  if tem_cmd ss; then
    # Conta linhas de conexões ESTABLISHED onde a porta local é :$port
    ss state established '( sport = :'"$port"' )' 2>/dev/null \
      | tail -n +2 | wc -l | awk '{print $1}'
  else
    # Fallback com netstat
    netstat -tan 2>/dev/null \
      | awk -v p=":$port" '$6=="ESTABLISHED" && $4 ~ p {c++} END{print c+0}'
  fi
}

limpa_processo() {
  local pid="$1"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" 2>/dev/null || true
    # Dá um tempo e força, se necessário
    sleep 0.1
    kill -9 "$pid" 2>/dev/null || true
  fi
}

# === Cabeçalho da saída ===
echo "backlog,conexoes_imediatas,rejeitadas" > "$OUT_CSV"
printf "\nTeste de backlog (PORT=%s, TEMPO_SLEEP=%s, clientes=%d)\n" "$PORT" "$TEMPO_SLEEP" "$CLIENTES_POR_RODADA"
printf "tcp_max_syn_backlog atual: %s\n\n" "$(tcp_max_syn_backlog)"
printf "%-8s | %-20s | %-10s\n" "BACKLOG" "CONEXOES_IMEDIATAS" "REJEITADAS"
printf -- "---------+----------------------+------------\n"

# === Loop principal ===
for BACKLOG in $(seq "$BACKLOG_INICIAL" "$BACKLOG_FINAL"); do
  # Sobe o servidor com o backlog corrente
  "$SERV_BIN" "$PORT" "$BACKLOG" "$TEMPO_SLEEP" >/dev/null 2>&1 &
  SERV_PID=$!

  # Garante limpeza se o script for interrompido
  trap 'limpa_processo "$SERV_PID"' INT TERM EXIT

  # Pequena pausa para o socket ficar pronto
  sleep "$PAUSA_APOS_START"

  # Dispara CLIENTES_POR_RODADA clientes simultaneamente
  # Cada cliente retorna 0 se conectou (sucesso), !=0 se falhou (rejeitado/erro).
  # Usamos xargs -P para paralelismo real.
  RESULTS_FILE="$(mktemp)"
  seq "$CLIENTES_POR_RODADA" \
    | xargs -I{} -P"$CLIENTES_POR_RODADA" bash -c '
        "'"$CLI_BIN"'" >/dev/null 2>&1 && echo OK || echo FAIL
      ' > "$RESULTS_FILE"

  # Espera um pouco para que as conexões que de fato "subiram" apareçam como ESTABLISHED
  sleep "$PAUSA_POS_CLIENTES"

  # Mede quantas estão imediatamente em ESTABLISHED
  ESTAB="$(conta_established "$PORT")"

  # Conta rejeitadas a partir do retorno dos clientes
  REJEITADAS="$(grep -c '^FAIL$' "$RESULTS_FILE" || true)"

  # Registra resultados
  echo "$BACKLOG,$ESTAB,$REJEITADAS" >> "$OUT_CSV"
  printf "%-8s | %-20s | %-10s\n" "$BACKLOG" "$ESTAB" "$REJEITADAS"

  # Limpa
  rm -f "$RESULTS_FILE"
  limpa_processo "$SERV_PID"
  trap - INT TERM EXIT
done

printf "\nResultados salvos em: %s\n" "$OUT_CSV"
printf "Valor de referência (tcp_max_syn_backlog): %s\n" "$(tcp_max_syn_backlog)"

# Dica opcional: confira também /proc/sys/net/core/somaxconn, que limita a fila de accept do listen()
# e pode interagir com o backlog informado pela aplicação.

