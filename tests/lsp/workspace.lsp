# The check of the files nobody opened: once the client says `initialized`, and without a delay, the file with a problem
# is published and the others are not - while no message waits, so the rest of the session waits for it
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"{root}","capabilities":{},"initializationOptions":{"workspaceDiagnosticsDelay":0}}}
{"jsonrpc":"2.0","method":"initialized","params":{}}
await 2
{"jsonrpc":"2.0","id":2,"method":"shutdown","params":null}
{"jsonrpc":"2.0","method":"exit","params":null}
