# Copyright (C) 2013 Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Lexer for Blink IDL.

The lexer uses the PLY (Python Lex-Yacc) library to build a tokenizer which
understands the Blink dialect of Web IDL and produces a token stream suitable
for the Blink IDL parser.

Blink IDL is identical to Web IDL at the token level, but the base lexer
does not discard comments. We need to override (and not include comments in
the token stream), as otherwise comments must be explicitly included in the
phrase grammar of the parser.

FIXME: Change base lexer to discard comments, and simply used the base
lexer, eliminating this separate lexer.

Web IDL:
    http://www.w3.org/TR/WebIDL/
Web IDL Grammar:
    http://www.w3.org/TR/WebIDL/#idl-grammar
PLY:
    http://www.dabeaz.com/ply/
"""

# Disable attribute validation, as lint can't import parent class to check
# pylint: disable=E1101

import os.path
import sys

# Base lexer is in Chromium src/tools/idl_parser
module_path, module_name = os.path.split(__file__)
tools_dir = os.path.join(module_path, os.pardir, os.pardir, os.pardir, os.pardir, os.pardir, os.pardir, 'tools')
sys.path.append(tools_dir)

from idl_parser.idl_lexer import IDLLexer

REMOVE_TOKENS = ['COMMENT']


class BlinkIDLLexer(IDLLexer):
    # ignore comments
    def t_COMMENT(self, t):
        r'(/\*(.|\n)*?\*/)|(//.*(\n[ \t]*//.*)*)'
        self.AddLines(t.value.count('\n'))

    # Analogs to _AddToken/_AddTokens in base lexer
    # Needed to remove COMMENT token, since comments ignored
    def _RemoveToken(self, token):
        if token in self.tokens:
            self.tokens.remove(token)

    def _RemoveTokens(self, tokens):
        for token in tokens:
            self._RemoveToken(token)

    def __init__(self):
        IDLLexer.__init__(self)
        self._RemoveTokens(REMOVE_TOKENS)


# If run by itself, attempt to build the lexer
if __name__ == '__main__':
    lexer = BlinkIDLLexer()
