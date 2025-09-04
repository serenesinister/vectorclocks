# vectorclocks

Este repositório reúne 4 etapas de projeto desenvolvidos com foco em conceitos fundamentais de sistemas paralelos e concorrentes, como Relógios Vetoriais, Modelo Produtor-Consumidor e o Snapshots de Chandy-Lamport.

---

## E1 - Base Relógios Vetoriais

📎 Repositório: [projeto-ppc-serenesinister](https://github.com/DCOMP-UFS/projeto-ppc-serenesinister)

Implementação da estrutura base de **Relógios Vetoriais**, uma ferramenta essencial para rastrear a ordem causal de eventos em sistemas distribuídos.  
Principais recursos:

- Vetores de relógios para múltiplos processos
- Atualização em eventos locais, envio e recebimento de mensagens
- Registro da ordem causal entre eventos

---

## E2 - Modelo Produtor-Consumidor

📎 Repositório: [implementacao-do-modelo-produtor-consumidor-vrennen](https://github.com/DCOMP-UFS/implementacao-do-modelo-produtor-consumidor-vrennen)

Implementação do clássico **modelo Produtor-Consumidor** utilizando threads e filas sincronizadas.

Principais recursos:

- Uso de threads para produtores e consumidores
- Fila com controle de concorrência
- Comunicação entre processos simulada

---

## E3 - Integração: Produtor-Consumidor + Relógios Vetoriais

📎 Repositório: [integrar-produtor-consumidor-com-os-relogios-vetoriais-serenesinister](https://github.com/DCOMP-UFS/integrar-produtor-consumidor-com-os-relogios-vetoriais-serenesinister)

Integra os conceitos de **Relógios Vetoriais** ao modelo **Produtor-Consumidor**, permitindo rastrear a causalidade entre mensagens trocadas.

Principais recursos:

- Comunicação entre produtores e consumidores com vetores de tempo
- Log de eventos com ordenação causal
- Debug e visualização do estado vetorial

---

## E4 - Snapshots de Chandy-Lamport

📎 Repositório: [implementacao-dos-snaphots-de-chandy-lamport-serenesinister](https://github.com/DCOMP-UFS/implementacao-dos-snaphots-de-chandy-lamport-serenesinister)

Implementação do algoritmo de **Chandy-Lamport** para captura de snapshots consistentes em sistemas distribuídos.

Principais recursos:

- Captura do estado local de cada processo
- Registro de canais e mensagens em trânsito
- Detecção de marcações (markers) conforme o algoritmo

---
