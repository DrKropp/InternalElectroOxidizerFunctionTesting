/* Initialize WebSocket Connection when the web interface is fully loaded in a browser
Handles data exchange between HTML and firmware

Adapted From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders
And From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders/ */

var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var isArmed = true; // is Armed - false = no / true = yes
var isS = false; // is in seconds mode - false = no / true = yes

window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
    initButton();

    document.querySelector('.card-grid').addEventListener('click', function(e) {
        const card = e.target.closest('.card');
        if (card && !e.target.closest('.timing-toggle')) {
            selectCard(card);
        }
    });
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
    setInterval(() =>  {
        if(websocket.readyState === websocket.OPEN){
            websocket.send("getValues");
        }
    }, 5000); // requests data every 5 seconds
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
    for (var i = 0; i < keys.length; i++){
        var key = keys[i];
        if(document.getElementById(key) === null) {
            console.warn("Element with ID " + key + " not found in the document.");
            continue;
        }

        if(isS && (key == "FValue2" || key == "RValue2")){
            document.getElementById(key).innerHTML = (myObj[key] / 1000).toFixed(2);
        } else {
            document.getElementById(key).innerHTML = myObj[key];
        }
    }
}

function initButton() {
    document.getElementById('off-button').addEventListener('click', toggleOff);
    document.getElementById('on-button').addEventListener('click', toggleOn);
    document.getElementById('update-button').addEventListener('click', handleUpdate);
    document.querySelector('.timing-toggle').addEventListener('click', function(e) {
        e.stopPropagation();
    });
}

function toggleOff() {
    if(!isArmed) { return; }
    isArmed = false;
    document.getElementById('state').innerHTML = "OFF";
    document.querySelector('.bottom-card').style.backgroundColor = "red";
    document.getElementById('on-button').classList.remove('active');
    document.getElementById('off-button').classList.add('active');
    websocket.send('toggle');
}

function toggleOn() {
    if(isArmed) { return; }
    isArmed = true;
    document.getElementById('state').innerHTML = "ON";
    document.querySelector('.bottom-card').style.backgroundColor = "green";
    document.getElementById('on-button').classList.add('active');
    document.getElementById('off-button').classList.remove('active');
    websocket.send('toggle');
}
  function toggle(){
    //websocket.send('toggle');
    if(isArmed){
        isArmed = false;
        document.getElementById('state').innerHTML = "OFF";
        document.querySelector('.bottom-card').style.backgroundColor = "red";
    } else {
        isArmed = true;
        document.getElementById('state').innerHTML = "ON";
        document.querySelector('.bottom-card').style.backgroundColor = "green";
    }
    websocket.send('toggle');
  }


function handleUpdate() {
    if(isArmed) {
        alert("Cannot change values while device output is on!");
        return;
    }
    if (!selectedCard || isArmed) return;
    
    const slider = document.getElementById("slider");
    const newValue = parseFloat(slider.value);
    const oldValueSpan = document.querySelector('.updated-value-container .tile:first-child span');
    
    // Convert back to ms for card2 if in seconds mode
    let valueToSend = newValue;
    let displayValue = newValue;
    
    if (selectedCardId === '2' && isS) {
        valueToSend = Math.round(newValue * 1000);  // convert to ms
        displayValue = newValue;                    // keep display in seconds
    }

    // update the display
    document.getElementById(selectedCardState + "Value" + selectedCardId).textContent = 
        selectedCardId === '2' && isS ? displayValue.toFixed(2) : displayValue;

    // send value to websocket server (always in ms for timing)
    websocket.send(selectedCardId + selectedCardState + valueToSend.toString());
    
    oldValueSpan.textContent = displayValue.toFixed(2);
    selectCard(selectedCard);
}

var selectedCard; // the acutal html element for the selected card
var selectedCardId; // the id of the selected card: 1-4
var selectedCardState = 'F'; // 'F' = Forward, 'R' = Reverse - 'F' is default

function selectCard(element){
    if(isArmed){
        alert("Cannot change values while device output is on!");
        return;
    }
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
    } else {
        if(selectedCard != null){
            document.getElementById(selectedCardState+"Value"+selectedCardId).classList.remove("selected");
            selectedCard.classList.remove("selected-card");
        }
        selectedCardState = 'F';
        selectedCard = element;
        selectedCardId = element.id.charAt(element.id.length-1);
        document.getElementById(selectedCardState+"Value"+selectedCardId).classList.add("selected");
        if(selectedCardId == '1'){
            document.querySelectorAll('#OldUnit, #NewUnit').forEach(unit => {
                if(unit == document.querySelector('#NewUnit')){
                    unit.textContent = "V";
                } else {
                    unit.textContent = " V";
                }
            });            
        } else {
            document.querySelectorAll('#FUnit2, #RUnit2, #OldUnit, #NewUnit').forEach(unit => {
                if(unit == document.querySelector('#NewUnit')){
                    unit.textContent = isS ? "S" : "mS";
                } else {
                    unit.textContent = isS ? " S" : " mS";
                }
            });
        }
    }
    selectedCard.classList.add("selected-card");
    updateSlider();
}

function updateSlider() {
    if(selectedCard == null || isArmed) return;
    
    const valueElement = document.getElementById(selectedCardState + "Value" + selectedCardId);
    let inputValue = parseFloat(valueElement.textContent);
    const slider = document.getElementById("slider");
    const oldValueSpan = document.querySelector('.updated-value-container .tile:first-child span');
    const newValueText = document.getElementById("new");

    // get base attributes (always in milliseconds)
    let min = parseFloat(valueElement.getAttribute("data-min"));
    let max = parseFloat(valueElement.getAttribute("data-max"));
    let step = parseFloat(valueElement.getAttribute("data-step"));

    if (selectedCardId === '2' && isS) { // converts to seconds if in seconds mode
        min /= 1000;
        if(min < 0.1) min = 0.1;
        max /= 1000;
        step = 0.1;
    }

    // set the slider values
    slider.max = max;
    slider.min = min;
    slider.step = step;
    newValueText.step = step;
    newValueText.min = min;
    newValueText.max = max;
    slider.value = inputValue;
    
    // update value displays
    oldValueSpan.textContent = isS && selectedCardId === '2' ? inputValue.toFixed(2) : inputValue;
    newValueText.value = isS && selectedCardId === '2' ? inputValue.toFixed(2) : inputValue;
    
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

function toggleTiming(element) {
    if(element == null || selectedCardId == '1') return;
    if(element.classList.contains('timing-selected')) return;
        
    isS = !isS;
    if(isS){
        document.getElementById('seconds').classList.add('timing-selected');
        document.getElementById('milliseconds').classList.remove('timing-selected');
    } else {
        document.getElementById('milliseconds').classList.add('timing-selected');
        document.getElementById('seconds').classList.remove('timing-selected');
    }

    document.querySelector('#card2 .card-title').textContent = isS ? "Timing S" : "Timing mS";
    
    document.querySelectorAll('#FUnit2, #RUnit2, #OldUnit, #NewUnit').forEach(unit => {
        if(unit == document.querySelector('#NewUnit')){
            unit.textContent = isS ? "S" : "mS";
        } else {
            unit.textContent = isS ? " S" : " mS";
        }
    });
    
    document.querySelectorAll('#FValue2, #RValue2').forEach(valueElement => {
        let value = parseFloat(valueElement.textContent);
        if(value < 0.1) value = 0.1;
        
        if (isS) { // convert ms to seconds
            valueElement.textContent = (value / 1000).toFixed(2);
        } else { // convert seconds to ms
            valueElement.textContent = Math.round(value * 1000);
        }
    });
    
    if (selectedCard && selectedCardId === '2') {
        updateSlider();
    }
}
