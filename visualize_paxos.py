

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# 1) Carrega CSV
df = pd.read_csv('events.csv', parse_dates=['timestamp'])

# 2) Mapeia cada nó a uma posição vertical
raw_nodes = set(df['source'].tolist() + df['destination'].tolist())
# Filtra apenas valores inteiros (ignorando 'all' e outros não inteiros)
nodes = [int(n) for n in raw_nodes if str(n).isdigit()]
y_map = {nid: idx for idx, nid in enumerate(sorted(nodes, reverse=True))}

# --- Diagrama de Sequência ---
plt.figure(figsize=(12, 6))
plt.title('Diagrama de Sequência (Eventos RECV)')
# Linhas de vida
tmin, tmax = df['timestamp'].min(), df['timestamp'].max()
for nid, y in y_map.items():
    plt.hlines(y, tmin, tmax, linestyles='dotted', linewidth=0.8)

# Desenha setas para cada RECV
df_recv = df[df['action'] == 'RECV']
for _, row in df_recv.iterrows():
    src = int(row['source'])
    dst = int(row['destination'])
    t = row['timestamp']
    plt.annotate(
        '',
        xy=(t, y_map[dst]),
        xytext=(t, y_map[src]),
        arrowprops=dict(arrowstyle='->', linewidth=0.5)
    )

plt.yticks(list(y_map.values()), [f'Node {nid}' for nid in sorted(y_map.keys(), reverse=True)])
plt.xlabel('Timestamp')
plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S.%f'))
plt.gcf().autofmt_xdate()
plt.tight_layout()
plt.show()

# --- Dashboard: Histograma de Mensagens por Tipo ---
plt.figure(figsize=(10, 4))
plt.title('Contagem de Mensagens por Tipo ao Longo do Tempo')

# Cria uma coluna de janela de tempo (ex: janelas de 1 segundo)
df['time_sec'] = df['timestamp'].dt.floor('S')
counts = df.groupby(['time_sec', 'action']).size().unstack(fill_value=0)

# Plota cada tipo em linha
for action in counts.columns:
    plt.plot(counts.index, counts[action], label=action)

plt.xlabel('Timestamp (segundos)')
plt.ylabel('Número de Mensagens')
plt.legend()
plt.gcf().autofmt_xdate()
plt.tight_layout()
plt.show()
