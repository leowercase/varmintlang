"use strict"

const clamp = (n, min, max) =>
  Math.max(min, Math.min(n, max));

const main = document.getElementById("main");

// Initialize Ace
const editor = ace.edit("editor");
editor.setTheme("ace/theme/cloud9_day");
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

function resizeView(left) {
  let leftSectionWidth;

  if (left === null)
    leftSectionWidth = "1fr";
  else {
    const gutter = document.getElementsByClassName("ace_gutter")[0];

    // Leave a line gutter's width worth of space around the resizable area
    const margin = gutter.getBoundingClientRect().width,
          width = main.getBoundingClientRect().width;

    leftSectionWidth = clamp(left, margin, width - margin).toString() + "px";
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
