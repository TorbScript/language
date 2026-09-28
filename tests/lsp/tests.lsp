# `torbscript/tests` over standard input and output: the capability `initialize` answers with, the tests and groups of
# a file nobody opened - read from the disk, the ranges in UTF-16 - and `[]` for a file that is not there and for a URI
# that is no file
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"{root}","capabilities":{}}}
{"jsonrpc":"2.0","method":"initialized","params":{}}
{"jsonrpc":"2.0","id":2,"method":"torbscript/tests","params":{"textDocument":{"uri":"{root}/math.test.trb"}}}
{"jsonrpc":"2.0","id":3,"method":"torbscript/tests","params":{"textDocument":{"uri":"{root}/missing.test.trb"}}}
{"jsonrpc":"2.0","id":4,"method":"torbscript/tests","params":{"textDocument":{"uri":"untitled:Untitled-1"}}}
{"jsonrpc":"2.0","id":5,"method":"shutdown","params":null}
{"jsonrpc":"2.0","method":"exit","params":null}
