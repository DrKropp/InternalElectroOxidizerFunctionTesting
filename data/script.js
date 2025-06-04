/* Initialize WebSocket Connection when the web interface is fully loaded in a browser
Handles data exchange between HTML and firmware

Adapted From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders
And From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders/ */

var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var isArmed = false;
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

function onMessage(event) {
    console.log(event.data);
    var myObj = JSON.parse(event.data);
    var keys = Object.keys(myObj);
    var state;
    if (event.data == "1"){
        state = "ON";
        //document.querySelector('.top-card .state span').color = "green";
      }
      else{
        state = "OFF";
        //document.querySelector('.top-card .state span').color = "red";
      }
    // for (var i = 0; i < keys.length; i+2){
    //     var key = keys[i];
    //     var key2 = keys[i+1];
    //     document.getElementById(key).innerHTML = myObj[key];
    //     document.getElementById("FValue"+ (i).toString()).value = myObj[key];
    //     document.getElementById(key2).innerHTML = myObj[key];
    //     document.getElementById("RValue"+ (i).toString()).value = myObj[key2];
    // }
}

function initButton() {
    document.getElementById('button').addEventListener('click', toggle);
    document.getElementById('update-button').addEventListener('click', handleUpdate);
}

  function toggle(){
    //websocket.send('toggle');
    if(isArmed){
        isArmed = false;
        document.getElementById('state').innerHTML = "OFF";
        document.querySelector('.top-card').style.backgroundColor = "red";
    } else {
        isArmed = true;
        document.getElementById('state').innerHTML = "ON";
        document.querySelector('.top-card').style.backgroundColor = "green";
    }
    websocket.send('toggle');
  }


  function handleUpdate() {
    if(isArmed) alert("Cannot update while armed!");
    if (!selectedCard || isArmed) return;
    
    const slider = document.getElementById("slider");
    const newValue = parseFloat(slider.value);
    const oldValueSpan = document.querySelector('.updated-value-container .tile:first-child span');
    
    document.getElementById(selectedCardState + "Value" + selectedCardId).textContent = newValue;

    websocket.send(selectedCardId+selectedCardState+newValue.toString());
    
    oldValueSpan.textContent = newValue;
    selectCard(selectedCard);
    updateSlider();
}

var selectedCard; // the acutal html element for the selected card
var selectedCardId; // the id of the selected card: 1-4
var selectedCardState = 'F'; // 'F' = Forward, 'R' = Reverse - 'F' is default

function selectCard(element){
    if(isArmed) return;
    if(selectedCard == element){
        if(selectedCard == document.getElementById('card1')) return;
        document.getElementById(selectedCardState+"Value"+selectedCardId).classList.remove("selected");
        if(selectedCardState == 'F'){
            selectedCardState = 'R';
        }
        else{
            selectedCardState = 'F';
        }
        document.getElementById(selectedCardState+"Value"+selectedCardId).classList.add("selected");
    }
    else{
        if(selectedCard != null){
            document.getElementById(selectedCardState+"Value"+selectedCardId).classList.remove("selected");
            selectedCard.classList.remove("selected-card");
        }
        selectedCardState = 'F';
        selectedCard = element;
        selectedCardId = element.id.charAt(element.id.length-1);
        document.getElementById(selectedCardState+"Value"+selectedCardId).classList.add("selected");
    }
    selectedCard.classList.add("selected-card");
    updateSlider();
}

function updateSlider() {
    if(selectedCard == null || isArmed) return;
    
    const valueElement = document.getElementById(selectedCardState + "Value" + selectedCardId);
    const inputValue = parseFloat(valueElement.textContent);
    const slider = document.getElementById("slider");
    const oldValueSpan = document.querySelector('.updated-value-container .tile:first-child span');
    const newValueText = document.getElementById("new");

    // set slider values
    slider.max = parseFloat(valueElement.getAttribute("data-max"));
    slider.min = parseFloat(valueElement.getAttribute("data-min"));
    slider.step = parseFloat(valueElement.getAttribute("data-step"));
    newValueText.step = parseFloat(valueElement.getAttribute("data-step"));
    slider.value = inputValue;
    // update the value displays
    oldValueSpan.textContent = inputValue;
    newValueText.value = inputValue;
}

function updateValue() {
    if(selectedCard == null || isArmed) return;
    const inputValue = parseFloat(document.getElementById("slider").value);
    document.getElementById("new").value = inputValue;
}

function syncSlider(){
    if(selectedCard == null || isArmed) return;
    const slider = document.getElementById("slider");
    const inputValue = parseFloat(document.getElementById("new").value);
    slider.value = inputValue;
}