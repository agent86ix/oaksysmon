/*
 *  I don't want to heavily comment the HTML/JS in this file.
 *  Every byte is currently saved to SRAM, which means my comments
 *  inline with the HTML/JS cost me memory.
 * 
 *  I'm using C++11 raw strings to encode this HTML. 
 *  It is probably easier to store/read this from flash, but
 *  for the purposes of compilation simplicity, I'm using this
 *  method.
 *  
 *  This page will require the browser to have an active internet
 *  connection, to load the Bootstrap and jQuery files from the 'net.
 *  It's probably possible to serve these from the Oak, if desired.
 *  
 *  It would be better if this large variable were stored in flash,
 *  but there are all sorts of complications to moving things to 
 *  flash on the ESP8266.  I haven't worked through those, and I
 *  don't really plan to since I don't need the SRAM back.
 *  
 *  Similarly, I could minify the HTML and JS to save some bytes
 *  (maybe as much as 50%?) but again, don't need the memory.
 *  
 *  The buttons that send special characters (ie, TAB/ESC) use
 *  integer constants from the Arduino Keyboard library:
 *  https://github.com/arduino-libraries/Keyboard/blob/master/src/Keyboard.h
 *  This just makes my life easier so that I don't have to convert them.
 */
#include "sketch_oak_ui.h"
#include <Arduino.h>

const char htmlFile[] = R"END(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="X-UA-Compatible" content="IE=edge">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Oak System Monitor</title>

<link rel="stylesheet" href="//maxcdn.bootstrapcdn.com/bootstrap/3.3.6/css/bootstrap.min.css">

<link rel="stylesheet" href="//maxcdn.bootstrapcdn.com/bootstrap/3.3.6/css/bootstrap-theme.min.css">
<script src="//ajax.googleapis.com/ajax/libs/jquery/1.11.3/jquery.min.js"></script>
<script src="//maxcdn.bootstrapcdn.com/bootstrap/3.3.6/js/bootstrap.min.js"></script>
</head>
<body>
<div class="container">
<h1>Oak System Monitor</h1>

<h2>System Status</h2>

<h3>CPU Status</h3>
<div id="status-cpu"></div>

<h3>Memory Status</h3>
<div id="status-mem"></div>

<h3>Disk Status</h3>
<div id="status-dsk"></div>

<h3>USB Interface Status</h3>
<form class="form-horizontal">
<div class="form-group">
<label for="usb-reset" class="col-sm-2 control-label">USB State: <span id="usb-state"></span></label>
<div class="col-sm-5">
<input class="btn btn-default" type="button" id="usb-reset" value="Reset">
</div>
</div>
</form>

<form class="form-horizontal">
<div class="form-group">
<label class="col-sm-2 control-label">Report Status:</label>
<label class="col-sm-5 control-label" style="text-align:left" id="rpt-status">Loading...</label>
</div>
</form>

<h2>Remote Keyboard</h2>
<form class="form-horizontal">
<div class="form-group">
  <label for="kbd-str" class="col-sm-2 control-label">String to Send</label>
  <div class="col-sm-4"><input type="text" class="form-control" id="kbd-str"></div>
  <div class="col-sm-1"><input class="btn btn-default" type="button" id="kbd-send" value="Send"></div>
</div>

<div class="form-group">
<label for="kbd-shift" class="col-sm-2 control-label">Modifiers</label>
<div class="col-sm-5">
<label class="checkbox-inline">
  <input type="checkbox" class="kbd-mod" id="kbd-shift" value="kbd-shift-on" bit="2"> SHIFT
</label>
<label class="checkbox-inline">
  <input type="checkbox" class="kbd-mod" id="kbd-ctrl" value="kbd-ctrl-on" bit="1"> CTRL
</label>
<label class="checkbox-inline">
  <input type="checkbox" class="kbd-mod" id="kbd-alt" value="kbd-alt-on" bit="4"> ALT
</label>
<label class="checkbox-inline">
  <input type="checkbox" class="kbd-mod" id="kbd-gui" value="kbd-gui-on" bit="8"> WIN/CMD
</label>
</div>
</div>

<div class="form-group">
<label for="kbd-tab" class="col-sm-2 control-label">Special Keys</label>
<div class="col-sm-5">
<input class="btn btn-default kbd-spec" type="button" id="kbd-tab" value="Tab" chr="179">
<input class="btn btn-default kbd-spec" type="button" id="kbd-esc" value="Esc" chr="177">
<input class="btn btn-default kbd-spec" type="button" id="kbd-esc" value="Return" chr="176">
</div>
</div>
</form>
</div>
<script type="text/javascript">

function buildProgressBar(label, valCur, valMax, unit) {
  var pct = valCur/valMax*100
  pctStr = pct.toFixed(0)
  var width = ""
  if(pct != 0) width = "width:"+pct+"%"

  return `
  <div class="row">
  <div class="col-sm-2 text-right"><b>${label}</b></div>
  
  <div class="col-sm-5">
  <div class="progress">
  <div class="progress-bar" role="progressbar" aria-valuenow="${pctStr}" aria-valuemin="0" aria-valuemax="100" style="min-width: 2em; ${width};">${pctStr}%
  </div></div></div>

  <div class="col-sm-2 text-left"><b>${valCur.toFixed(0)}/${valMax.toFixed(0)} ${unit}</b></div>
  </div>
  `
}

function getMod() {
  var mod = 0;
  $(".kbd-mod").each(function () {
    if($(this).prop('checked')) mod |= parseInt($(this).attr('bit'))
  })
  return mod
}

function usbStateSuccess(json) {
  if('r' in json && json['r'] == 200 && 'state' in json) {
    if(json['state'] == 0) {
       $("#usb-state").html("Offline");
    } else {
      $("#usb-state").html("Online");
    }
  } else {
    $("#usb-state").html("Invalid");
  }
  setTimeout("getUsbState()",3000);
}

function usbStateError(error) {
  $("#usb-state").html("Error");
  setTimeout("getUsbState()",3000);
}

function getUsbState() {
  $.ajax('/usbstate', {
    success:usbStateSuccess,
    error:usbStateError
  })
}
function reportSuccess(json) {
  if(json == null || typeof json !== 'object') {
    $("#rpt-status").html("Invalid response from Oak!");
    setTimeout("getReport()",3000);
    return;
  }
  if('t' in json) {
    $("#rpt-status").html("Last update: "+json['t'])
  } else {
    $("#rpt-status").html("No update timestamp!");
  }
  if('c' in json) {
    var output = ''
    for(var i in json['c']) {
      output += buildProgressBar("CPU"+i, json['c'][i], 100, "")
    }
    $("#status-cpu").html(output)
  }
  if('m' in json) {
    var output = ''
    var used = (json['m']['t']-json['m']['a'])/(1024*1024)
    var total = json['m']['t']/(1024*1024)
    output = buildProgressBar("RAM", used, total, "MiB used")
    $("#status-mem").html(output)
  }
  if('d' in json) {
    var output = ''
    for(var i in json['d']) {
      var used = json['d'][i]['u']/(1024*1024*1024)
      var total = json['d'][i]['t']/(1024*1024*1024)
      output += buildProgressBar(json['d'][i]['m'], used, total, "GiB used")
    }
    $('#status-dsk').html(output)
  }
  setTimeout("getReport()",3000);
}

function reportError(xhr, status, error ) {
  $("#rpt-status").html("Error in the response from the Oak!");
  setTimeout("getReport()",3000);
}

function getReport() {
  $.ajax("/report", {
    success:reportSuccess,
    error:reportError
  })
}

function onKbdSuccess(json) {
  console.log(json)
}

$(document).ready(function () {
  $(".kbd-spec").click(function (e) {
    $.ajax('/kbdspec', {
      method: 'POST',
      success: onKbdSuccess,
      data: {mod:getMod(), chr:$(this).attr('chr')}
    })
  })

  $("#usb-reset").click(function (e) {
    $.ajax('/usbrst', {});
  })

  $("#kbd-send").click(function (e) {
    $.ajax('/kbdstr',{
      method: 'POST',
      success: onKbdSuccess,
      data: {mod:getMod(), str:$("#kbd-str").val()}
    })
  })

  getUsbState();
  getReport();
})
</script>

<script src="//maxcdn.bootstrapcdn.com/bootstrap/3.3.6/js/bootstrap.min.js"></script>
</body>
</html>
)END";
