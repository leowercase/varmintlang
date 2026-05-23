// The docs are quite cryptic.
// https://github.com/ajaxorg/ace/wiki/Creating-or-Extending-an-Edit-Mode
"use strict"

ace.define("varmint", (require, exports, module) => {
  const oop = require("ace/lib/oop");

  // Parent mode
  const TextMode = require("ace/mode/text").Mode;
  //const { MatchingBraceOutdent } = require("ace/mode/matching_brace_outdent");

  const { VarmintHighlightRules } = require("varmint_highlight_rules");

  const Mode = function() {
    this.HighlightRules = VarmintHighlightRules;
    //this.outdent = new MatchingBraceOutdent();
  };
  oop.inherits(Mode, TextMode);

  (function() {
    // Configure comments
    this.lineCommentStart = "#";
    this.blockComment = { start: "#[", end: "]#" };
  })
    .call(Mode.prototype);

  exports.Mode = Mode;
});

ace.define("varmint_highlight_rules", (require, exports, module) => {
  const oop = require("ace/lib/oop");
  const { TextHighlightRules } = require("ace/mode/text_highlight_rules");

  const VarmintHighlightRules = function() {
    const keywords = {
      // Keywords
      "keyword.control":
          "if|then|else|elif|loop|for|while|break|continue|return",
      "keyword":
          "in|do",
      "storage.type":
          "as",
      // Builtins
      "variable.language":
          "e|pi|tau|inf|" +
          "abs|sqrt|cbrt|ln|lg|sin|cos|tan|asin|acos|atan|ceil|floor|round|trunc|rand|" +
          "typeof|len|" +
          "push|pop|" +
          "to_number|to_string|unwrap|" +
          "has|" +
          "put|putln|input|" +
          "time|" +
          "range|items|" +
          "char_ord|asciify|" +
          "rot",
      // Constants
      "constant.language":
          "Some|None",
      "constant.language.boolean":
          "True|False",
    };

    const identifierRegex = /[a-zA-Z_][a-zA-Z_\d]*'*/;

    // Comments and whitespace
    const redundant = [
      {
        token: "comment.block",
        regex: /#\[(?!])/,
        push: "block-comment",
      },
      {
        token: "comment.line",
        regex: /#.*$/,
      },
      {
        token: "text",
        regex: /\s+/,
      },
    ]

    this.$rules = {
      "start": [
        redundant,

        // var x
        {
          token: "storage.type",
          regex: "var",
          push: "var-bind",
        },
        {
          regex: /\,/,
          // Exit from the declarator back into the `var`
          onMatch: function(_, state, stack) {
            if (stack.length >= 2 && stack[1] === "var-bind") {
              stack.shift(); stack.shift();
              this.next = "var-bind";
            }
            return "punctuation.operator";
          },
        },

        // (x, y) =>
        {
          token: ["paren.lparen", "text", "variable.parameter", "text", "punctuation.operator"],
          regex: `(\\()(\\s*)(${identifierRegex.source})(\\s*)(,)`,
          push: "argument-list",
        },
        // (x) =>
        {
          token: ["paren.lparen", "text", "variable.parameter", "text", "paren.rparen", "text", "keyword.operator"],
          regex: `(\\()(\\s*)(${identifierRegex.source})(\\s*)(\\))(\\s*)(=>)`,
        },
        // x =>
        {
          token: ["variable.parameter", "text", "keyword.operator"],
          regex: `(${identifierRegex.source})(\\s*)(=>)`,
        },

        {
          token: this.createKeywordMapper(keywords, "identifier"),
          regex: identifierRegex,
        },
        {
          token: "variable.other", // Loop label
          regex: "'" + identifierRegex.source,
        },

        {
          token: "constant.numeric",
          regex: /\d+\.+\d+/,
        },
        {
          token: "constant.numeric",
          regex: /\d+/,
        },
        {
          token: "string",
          regex: "\"",
          next: "string",
        },

        {
          token: "keyword.operator",
          regex: /[+\-*/^%!]|(\|\|)|(\|\*)|(\/?=)|([<>]=?)|(:=)|(->)|(=>)|(\?!)/,
        },
        {
          token: "punctuation.operator",
          regex: /\??\.|[,:;]/,
        },
        {
          token: "paren.lparen",
          regex: /[({]|\??\[/,
        },
        {
          token: "paren.rparen",
          regex: /[)\]}]/,
        },
      ],

      "block-comment": [
        {
          token: "comment.block",
          regex: /#\[(?!])/,
          push: "block-comment",
        },
        {
          token: "comment.block",
          regex: /\]#/,
          next: "pop",
        },
        { defaultToken: "comment.block" },
      ],

      "var-bind": [
        redundant,
        {
          token: "keyword",
          regex: "in",
          next: "pop",
        },
        {
          token: ["entity.name.function", "text", "paren.lparen"],
          regex: `(${identifierRegex.source})(\\s*)(\\()`,
          push: "argument-list",
        },
        {
          token: ["identifier", "text", "keyword.operator"],
          regex: `(${identifierRegex.source})(\\s*)(:=)`,
          push: "start",
        },
        {
          token: "empty",
          regex: "",
          next: "pop",
        }
      ],

      "argument-list": [
        redundant,
        {
          token: "punctuation.operator",
          regex: /,/,
        },
        {
          token: "variable.parameter",
          regex: identifierRegex,
        },
        {
          token: ["paren.rparen", "text", "keyword.operator"],
          regex: /(\))(\s*)(:=)/,
          // Assume this is a `var` expression.
          // Exit from the argument list into the declarator
          next: (_, stack) => {
            stack.shift(); stack.shift();
            return "start";
          },
        },
        {
          token: "paren.rparen",
          regex: /\)/,
          next: "pop",
        },
        {
          token: "empty",
          regex: "",
          next: "pop",
        },
      ],

      "string": [
        {
          token: "string",
          regex: "\"",
          next: "start",
        },
        { defaultToken: "string" },
      ],
    };

    // Makes push and pop work!
    this.normalizeRules();
  };
  oop.inherits(VarmintHighlightRules, TextHighlightRules);

  exports.VarmintHighlightRules = VarmintHighlightRules;
});
