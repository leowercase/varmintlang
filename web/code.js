"use strict"

const clamp = (n, min, max) =>
  Math.max(min, Math.min(n, max));

const main = document.getElementById("main");

// Initialize Ace
const editor = ace.edit("editor");
editor.setTheme("ace/theme/cloud9_day");
editor.session.setMode("ace/mode/c_cpp");

const initialText =
`var fib(len) :=
    var i := 0,
        prev := 0, prev' := 1
    in
    () => {
        if i >= len
          then return None

        var result

        if i = 0
          then result := 0
        else {
          result := prev + prev'
          prev' := prev
          prev := result
        }

        i +:= 1
        Some(result)
    }

for fib_i in fib 20
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
          width = main.getBoundingClientRect().width;

    leftSectionWidth = clamp(x, margin, width - margin).toString() + "px";
  }

  main.style.setProperty("--left-section-width", leftSectionWidth);
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

const stdout = document.getElementById("stdout");

function run() {
  stdout.textContent = "Voila!";
}

function stop() {
  stdout.textContent = "Stopping";
}

function dis() {
  stdout.textContent = "01 INSTRUCTION [x] = y";
}

document.getElementById("btn-run").addEventListener("click", run);
document.getElementById("btn-stop").addEventListener("click", stop);
document.getElementById("btn-dis").addEventListener("click", dis);

const stdin = document.getElementById("stdin"),
      stdinPrompt = document.getElementById("stdin-prompt");

stdinPrompt.addEventListener("keydown", ev => {
  if (ev.key === "Enter") {
    ev.preventDefault();
    stdin.submit();
  }
});
