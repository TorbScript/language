# The features of the second round over standard input and output: the outline of a document, signature help, a hover
# with the documentation of `torb doc` and its link, the references of a function in a file nobody opened, the layout
# of `torb format`, the symbols of the workspace, and a checked rename - the requests whose answer takes steps are
# answered before `shutdown` is
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"{root}","capabilities":{"textDocument":{"documentSymbol":{"hierarchicalDocumentSymbolSupport":true}},"workspace":{"workspaceEdit":{"documentChanges":true}}}}}
{"jsonrpc":"2.0","method":"initialized","params":{}}
open src/main.trb
{"jsonrpc":"2.0","id":2,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"{root}/src/main.trb"}}}
{"jsonrpc":"2.0","id":3,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"{root}/src/main.trb"},"position":{"line":7,"character":19}}}
{"jsonrpc":"2.0","id":4,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/src/main.trb"},"position":{"line":4,"character":12}}}
{"jsonrpc":"2.0","id":5,"method":"textDocument/references","params":{"textDocument":{"uri":"{root}/src/main.trb"},"position":{"line":4,"character":12},"context":{"includeDeclaration":true}}}
{"jsonrpc":"2.0","id":6,"method":"textDocument/formatting","params":{"textDocument":{"uri":"{root}/src/main.trb"},"options":{"tabSize":2,"insertSpaces":true}}}
{"jsonrpc":"2.0","id":7,"method":"workspace/symbol","params":{"query":"double"}}
{"jsonrpc":"2.0","id":8,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"{root}/src/main.trb"},"position":{"line":4,"character":12}}}
{"jsonrpc":"2.0","id":9,"method":"textDocument/rename","params":{"textDocument":{"uri":"{root}/src/main.trb"},"position":{"line":4,"character":12},"newName":"twice"}}
{"jsonrpc":"2.0","id":10,"method":"shutdown","params":null}
{"jsonrpc":"2.0","method":"exit","params":null}
