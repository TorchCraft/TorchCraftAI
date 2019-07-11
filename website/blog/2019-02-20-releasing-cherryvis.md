---
title: Releasing CherryVis: a replay viewer for TorchCraftAI
author: Danthe3rd
authorURL: https://github.com/danthe3rd
authorFBID: 0
---
<!-- Carousel code 1/2 -->
<style>
.slide {display:none; width:100%; text-align:center;}
.slide img { height: 450px;}
.w3-center{text-align:center!important}
.w3-section,.w3-code{margin-top:16px!important;margin-bottom:16px!important}
.w3-btn,.w3-button{border:none;display:inline-block;padding:8px 16px;vertical-align:middle;overflow:hidden;text-decoration:none;color:inherit;background-color:inherit;text-align:center;cursor:pointer;white-space:nowrap;}
.w3-button{-webkit-touch-callout:none;-webkit-user-select:none;-khtml-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none}   
.w3-button:hover{color:#000!important;background-color:#ccc!important}
.w3-red,.w3-hover-red:hover{color:#fff!important;background-color:#f44336!important}
.w3-light-grey,.w3-hover-light-grey:hover,.w3-light-gray,.w3-hover-light-gray:hover{color:#000!important;background-color:#f1f1f1!important}
</style>


We're glad to announce today that we are open sourcing CherryVis.
Based on OpenBW's [replay viewer](http://www.openbw.com/replay-viewer/), CherryVis allows you to to view StarCraft replays alongside information about the bot's state.
It's a powerful tool to simultaneously understand, develop and debug agents built with TorchCraftAI, as it gives access to:
  - The blackboard: a place where we store information shared between modules, such as the current build order
  - The logs
  - For any unit: its ID, HP, task - or even unit-specific log messages
  - The tree of [UPC tuples](https://torchcraft.github.io/TorchCraftAI/docs/core-abstractions.html) issued


<div class="w3-content" style="max-width:800px">
  <a class="vis1-slide slide" href="/TorchCraftAI/blog/assets/cvis-interface.png"><img src="/TorchCraftAI/blog/assets/cvis-interface.png"/></a>
  <a class="vis1-slide slide" href="/TorchCraftAI/blog/assets/cvis-tasks.png"><img src="/TorchCraftAI/blog/assets/cvis-tasks.png"/></a>
  <a class="vis1-slide slide" href="/TorchCraftAI/blog/assets/cvis-upc-tree.png"><img src="/TorchCraftAI/blog/assets/cvis-upc-tree.png"/></a>
</div>
<div class="w3-center">
  <div class="w3-section">
    <button class="w3-button w3-light-grey" onclick="plusDivs(-1, 'vis1')">❮ Prev</button>
    <button class="w3-button w3-light-grey" onclick="plusDivs(1, 'vis1')">Next ❯</button>
  </div>
  <button class="w3-button vis1-dot" onclick="showDiv(1, 'vis1')">1</button>
  <button class="w3-button vis1-dot" onclick="showDiv(2, 'vis1')">2</button>
  <button class="w3-button vis1-dot" onclick="showDiv(3, 'vis1')">3</button>
</div>


#### Machine learning visualization
In a machine learning context, it is often difficult to interpret a model's outputs, or understand if intermediate results are reasonable. That's why CherryVis includes
features useful to researchers:
  - Arbitrary trees visualization. For example for [Monte Carlo trees](https://en.wikipedia.org/wiki/Monte_Carlo_tree_search)
  - Tensor summaries. Tensor values are displayed in a histogram, dynamically updated as the game progresses
  - Heatmaps game overlay
  - .. and many more

<div class="w3-content" style="max-width:800px">
  <a class="vis2-slide slide" href="/TorchCraftAI/blog/assets/cvis-buildability.png"><img src="/TorchCraftAI/blog/assets/cvis-buildability.png"/></a>
  <a class="vis2-slide slide" href="/TorchCraftAI/blog/assets/cvis-values-distr.png"><img src="/TorchCraftAI/blog/assets/cvis-values-distr.png"/></a>
  <a class="vis2-slide slide" href="/TorchCraftAI/blog/assets/cvis-ground-height.png"><img src="/TorchCraftAI/blog/assets/cvis-ground-height.png"/></a>
</div>
<div class="w3-center">
  <div class="w3-section">
    <button class="w3-button w3-light-grey" onclick="plusDivs(-1, 'vis2')">❮ Prev</button>
    <button class="w3-button w3-light-grey" onclick="plusDivs(1, 'vis2')">Next ❯</button>
  </div>
  <button class="w3-button vis2-dot" onclick="showDiv(1, 'vis2')">1</button>
  <button class="w3-button vis2-dot" onclick="showDiv(2, 'vis2')">2</button>
  <button class="w3-button vis2-dot" onclick="showDiv(3, 'vis2')">3</button>
</div>

#### Getting started
If you are curious about this tool, you can try it for yourself after setting up the [python dependencies and building OpenBW to Javascript](https://github.com/TorchCraft/TorchCraftAI/blob/master/scripts/cherryvis/README.md).
We also provide a few example replays with data for CherryVis, so you don't need to setup Starcraft or TorchCraftAI to understand how the bot works.


For advanced users who are already using TorchCraftAI, we provide an [example script](https://github.com/TorchCraft/TorchCraftAI/blob/master/test/modules/cherryvisdumper_t.cpp) dumping various data to CherryVis.


Note that CherryVis can be used outside of TorchCraftAI as well. A [zstd](https://github.com/facebook/zstd) compressed JSON trace file can be provided to benefit from its features and dump logs, tensors trees, ... see [CherryVisDumperModule::writeGameSummary](https://github.com/TorchCraft/TorchCraftAI/blob/master/src/modules/cherryvisdumper.cpp) for the format. Feel free to file a new [GitHub issue](https://github.com/TorchCraft/TorchCraftAI/issues) if you need help adapting CherryVis to your workflow.


<!-- Carousel code 2/2 -->
<script>
var slideIndex = {};
showDiv(1, 'vis1');
showDiv(1, 'vis2');
function plusDivs(n, name) {
  showDiv(slideIndex[name] + n, name);
}
function showDiv(n, name) {
  slideIndex[name] = n;
  var i;
  var x = document.getElementsByClassName(name + "-slide");
  var dots = document.getElementsByClassName(name + "-dot");
  if (n > x.length) {slideIndex[name] = 1}
  if (n < 1) {slideIndex[name] = x.length}
  for (i = 0; i < x.length; i++) {
    x[i].style.display = "none";
  }
  for (i = 0; i < dots.length; i++) {
    dots[i].className = dots[i].className.replace(" w3-red", "");
  }
  x[slideIndex[name]-1].style.display = "block";
  dots[slideIndex[name]-1].className += " w3-red";
}
</script>
