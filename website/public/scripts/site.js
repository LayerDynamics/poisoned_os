const copyLabel = "Copy";

document.querySelectorAll(".markdown pre").forEach((pre) => {
  const button = document.createElement("button");
  button.className = "copy-code";
  button.type = "button";
  button.textContent = copyLabel;
  button.addEventListener("click", async () => {
    await navigator.clipboard.writeText(pre.querySelector("code")?.textContent ?? pre.textContent ?? "");
    button.textContent = "Copied";
    window.setTimeout(() => { button.textContent = copyLabel; }, 1600);
  });
  pre.append(button);
});
