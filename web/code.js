"use strict"

const clamp = (n, min, max) =>
  Math.max(min, Math.min(n, max));

const main = document.getElementById("main");

// Initialize Ace
const editor = ace.edit("editor");
editor.setTheme("ace/theme/github_dark");
editor.session.setMode("ace/mode/c_cpp");

const initialText =
`#include <stdio.h>

int main(void) {
  printf("hello, world!");
  return 0;
}`;
editor.setValue(initialText, -1);

const sep = document.getElementById("sep");
let mouseDown = false, mouseX = 0;

// Amount of space left around the resizable area
const margin = 40;

function resizeView(left) {
  const width = main.getBoundingClientRect().width;
  left = clamp(left, margin, width - margin);

  main.style.gridTemplateColumns =
    `[left-start] ${left}px [left-end] 4px [right-start] 1fr [right-end]`;

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
  mouseDown = true
});

document.addEventListener("mousemove", ev => {
  if (mouseDown) {
    mouseX = ev.clientX;

    resizeView(mouseX);
  }
});

const stdout = document.getElementById("stdout");

function run() {
  stdout.textContent = "Voila!";
}

const runBtn = document.getElementById("btn-run");
runBtn.addEventListener("click", run);

const disBtn = document.getElementById("btn-dis");
disBtn.addEventListener("click", run);

const tokensBtn = document.getElementById("btn-tokens");
tokensBtn.addEventListener("click", run);

const stdin = document.getElementById("stdin"),
      stdinPrompt = document.getElementById("stdin-prompt");

stdinPrompt.addEventListener("keydown", ev => {
  if (ev.key === "Enter") {
    ev.preventDefault();
    stdin.submit();
  }
});
