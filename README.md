# Servidor de Arquivos - Trabalho 2

Implementação de uma aplicação cliente-servidor para manipulação distribuída de arquivos usando API de Sockets em C.

## Características

- **Múltiplos clientes simultâneos**: Suporta múltiplas conexões usando threads (pthread)
- **GET Condicional**: Similar ao HTTP, baixa arquivo apenas se a versão do servidor for mais recente
- **PUT**: Envia arquivo local para o servidor (substitui se já existir)
- **DELETE**: Remove arquivo do servidor
- **Códigos de Status**: Todas as respostas incluem código de status (200, 304, 404, 500)

## Compilação

```bash
make all
```

Para limpar os binários e arquivos:
```bash
make clean
```

## Uso

### Iniciar o Servidor

```bash
./server <porta>
```

Exemplo:
```bash
./server 8080
```

O servidor cria automaticamente o diretório `server_files/` onde os arquivos são armazenados.

### Cliente

```bash
./client <ip> <porta> <comando> <arquivo>
```

#### Comando GET

Baixa um arquivo do servidor. Se o arquivo já existe localmente, envia o timestamp da última modificação e só baixa se a versão do servidor for mais recente.

```bash
./client 127.0.0.1 8080 GET arquivo.txt
```

**Códigos de resposta:**
- `200 OK`: Arquivo baixado com sucesso
- `304 Not Modified`: Arquivo local está atualizado
- `404 File not found`: Arquivo não existe no servidor

#### Comando PUT

Envia um arquivo local para o servidor. O arquivo deve estar no diretório `client_files/`.

```bash
./client 127.0.0.1 8080 PUT arquivo.txt
```

**Códigos de resposta:**
- `200 OK`: Arquivo enviado com sucesso
- `500 Error`: Erro ao criar arquivo no servidor
- `500 Incomplete file transfer`: Conexão interrompida antes de completar o upload (arquivo parcial removido automaticamente)

#### Comando DELETE

Remove um arquivo do servidor.

```bash
./client 127.0.0.1 8080 DELETE arquivo.txt
```

**Códigos de resposta:**
- `200 OK`: Arquivo deletado com sucesso
- `404 File not found`: Arquivo não existe no servidor

## Estrutura de Diretórios

```
.
├── server.c              # Código do servidor
├── client.c              # Código do cliente
├── Makefile              # Script de compilação
├── server_files/         # Arquivos no servidor (criado automaticamente)
└── client_files/         # Arquivos do cliente (criado automaticamente)
```

## Protocolo de Comunicação

### Formato de Requisição

**GET:**
```
GET <arquivo> <timestamp>\n
```

**PUT:**
```
PUT <arquivo>\n
<tamanho>\n
<dados do arquivo>
```

**DELETE:**
```
DELETE <arquivo>\n
```

### Formato de Resposta

```
<código> <mensagem>\n
[dados adicionais se aplicável]
```

## Exemplos de Uso

### Criar arquivos de teste

```bash
make test_files
```

Isso cria arquivos de exemplo em `client_files/`:
- `test.txt`
- `teste.txt`
- `sample.dat`

### Enviar arquivo para o servidor

```bash
./client 127.0.0.1 8080 PUT test.txt
```

### Baixar arquivo do servidor

```bash
./client 127.0.0.1 8080 GET test.txt
```

### Verificar cache (GET condicional)

Execute o comando GET novamente com o arquivo já existente:

```bash
./client 127.0.0.1 8080 GET test.txt
```

Saída esperada: `Status: 304 Not Modified`

### Deletar arquivo do servidor

```bash
./client 127.0.0.1 8080 DELETE test.txt
```

## Notas Técnicas

- **Thread Safety**: Operações de arquivo são protegidas por mutex para evitar condições de corrida
- **Porta 8080**: Porta padrão recomendada (pode ser alterada)
- **Tamanho máximo de requisição**: 64KB
- **Buffer de transferência**: 4KB para otimizar transferência de arquivos grandes
- **Timestamp**: Utiliza `st_mtime` para comparação de versões de arquivos

## Requisitos do Sistema

- GCC com suporte a pthread
- Linux/Unix (testado em NixOS)
- Bibliotecas: socket, pthread, sys/stat

## Autores

Trabalho desenvolvido para a disciplina de Redes de Computadores - ICT/UNIFESP
