/* Initialize WebSocket Connection when the web interface is fully loaded in a browser
Handles data exchange between HTML and firmware

Adapted From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders
And From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders/ */

var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
    initButton();
}

function getValues(){
    websocket.send("getValues");
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    getValues();
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function updateSliderPWM(element) {
    var sliderNumber = element.id.charAt(element.id.length-1);
    var sliderValue = document.getElementById(element.id).value;
    document.getElementById("value"+sliderNumber).value = sliderValue;
    console.log(sliderValue);
    websocket.send(sliderNumber+"s"+sliderValue.toString());
}

function updateSlider(element){
    var inputNumber = element.id.charAt(element.id.length-1);
    var inputValue = document.getElementById(element.id).value;
    document.getElementById("slider"+inputNumber).value = inputValue;
    console.log(inputValue);
    websocket.send(inputNumber+"s"+inputValue.toString());
}

function onMessage(event) {
    console.log(event.data);
    var myObj = JSON.parse(event.data);
    var keys = Object.keys(myObj);
    var state;
    if (event.data == "1"){
        state = "ON";
      }
      else{
        state = "OFF";
      }
    for (var i = 0; i < keys.length; i++){
        var key = keys[i];
        document.getElementById(key).innerHTML = myObj[key];
        document.getElementById("slider"+ (i+1).toString()).value = myObj[key];
    }
}

function initButton() {
    document.getElementById('button').addEventListener('click', toggle);
  }

  function toggle(){
    websocket.send('toggle');
  }