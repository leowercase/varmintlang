"use strict"

import Varmint from "./result/bin/varmint.mjs"

const clamp = (n, min, max) =>
  Math.max(min, Math.min(n, max));

const mainContent = document.getElementById("main");

// Initialize Ace
const editor = ace.edit("editor", {
  theme: "ace/theme/chrome",
  mode: "varmint",
  newLineMode: "unix",
});

const initialText =
`# Let's generate some Fibonacci numbers!

var fib(sequence_length) :=
  var i := 0,
      prev := 0, prev' := 1
  in
  () =>
    if i < sequence_length then {
      var result

      if i > 0 then {
        result := prev + prev'
        prev' := prev
        prev := result
      }
      else result := 0

      i +:= 1
      result
    }

var prompt_num(msg) :=
  input(msg):to_number() else prompt_num msg

for fib_i in fib prompt_num "Fibonacci sequence length:"
  do putln "\\(fib_i)"
`;
editor.setValue(initialText, -1);

const sep = document.getElementById("sep");
let mouseDown = false;

function resizeView(x) {
  let leftSectionWidth;

  if (x === null)
    leftSectionWidth = "1fr";
  else {
    const gutter = document.getElementsByClassName("ace_gutter")[0];

    // Leave a line gutter's width worth of space around the resizable area
    const margin = gutter.getBoundingClientRect().width,
          width = mainContent.getBoundingClientRect().width;

    leftSectionWidth = clamp(x, margin, width - margin).toString() + "px";
  }

  mainContent.style.setProperty("--left-section-width", leftSectionWidth);
  editor.resize();
}

function selectable(userSelect) {
  document.body.style["user-select"]
    = document.body.style["-webkit-user-select"] = userSelect;
}

document.addEventListener("mouseup", () => {
  selectable("auto");
  mouseDown = false;
});
sep.addEventListener("mousedown", () => {
  selectable("none");
  mouseDown = true;
});

document.addEventListener("mousemove", ev => {
  if (mouseDown) resizeView(ev.clientX);
});
sep.addEventListener("dblclick", () => resizeView(null));

const invoke = fn => () => {
  document.getElementById("stdout").innerHTML = "";
  fn(editor.getValue());
};

const vm = await Varmint();

const run = vm.cwrap("run", null, ["string"]);
const disassemble = vm.cwrap("dis", null, ["string"]);

const actions = {
  run: invoke(run),
  dis: invoke(disassemble),
};

for (const [action, fn] of Object.entries(actions)) {
  document.getElementById(`btn-${action}`).addEventListener("click", fn);
}
