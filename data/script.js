/* Initialize WebSocket Connection when the web interface is fully loaded in a browser
Handles data exchange between HTML and firmware

Adapted From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders
And From: https://randomnerdtutorials.com/esp32-web-server-websocket-sliders/ */

var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var isArmed = true; // is Armed - false = no / true = yes
var isS = false; // is in seconds mode - false = no / true = yes

window.addEventListener('load', onload);

function onload() {
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

var pollingInterval; // Store interval ID for cleanup

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
    websocket.onerror = function(error) {
        console.log('WebSocket error:', error);
    };

    // Clear any existing polling interval to prevent duplicates
    if (pollingInterval) {
        clearInterval(pollingInterval);
    }

    // Set up new polling interval - disabled since server pushes updates every 500ms
    // Client no longer needs to poll, server automatically sends updates
    // pollingInterval = setInterval(() => {
    //     if(websocket.readyState === WebSocket.OPEN) {
    //         websocket.send("getValues");
    //     }
    // }, 5000); // requests data every 5 seconds
}

function onOpen() {
    console.log('Connection opened');
    getValues();
}

function onClose() {
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

    // CRITICAL FIX: Handle isRunning state updates from server
    if (myObj.hasOwnProperty('isRunning')) {
        const serverIsRunning = myObj.isRunning;
        
        // Only update UI if state differs from current local state
        if (serverIsRunning !== isArmed) {
            isArmed = serverIsRunning;
            
            // Update UI to match server state
            if (isArmed) {
                document.getElementById('state').innerHTML = "ON";
                document.querySelector('#state-card').style.backgroundColor = "green";
                document.getElementById('on-button').classList.add('active');
                document.getElementById('off-button').classList.remove('active');
            } else {
                document.getElementById('state').innerHTML = "OFF";
                document.querySelector('#state-card').style.backgroundColor = "red";
                document.getElementById('on-button').classList.remove('active');
                document.getElementById('off-button').classList.add('active');
            }
            
            console.log('Device state synchronized: ' + (isArmed ? 'ON' : 'OFF'));
        }
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

    // Ensure WebSocket is open before sending command
    if(websocket.readyState !== WebSocket.OPEN) {
        console.error('WebSocket not connected');
        return;
    }

    // Send toggle command - don't update UI yet, wait for server confirmation
    websocket.send('toggle');
    console.log('Toggle OFF command sent');
}

function toggleOn() {
    if(isArmed) { return; }

    // Ensure WebSocket is open before sending command
    if(websocket.readyState !== WebSocket.OPEN) {
        console.error('WebSocket not connected');
        return;
    }

    // Send toggle command - don't update UI yet, wait for server confirmation
    websocket.send('toggle');
    console.log('Toggle ON command sent');
}


function handleUpdate() {
    if (!selectedCard) return;

    // Ensure WebSocket is open before sending command
    if(websocket.readyState !== WebSocket.OPEN) {
        console.error('WebSocket not connected');
        return;
    }

    const wasRunning = isArmed; // Remember if output was on

    // If output is running, turn it off first
    if (wasRunning) {
        console.log('Turning off output before update...');
        websocket.send('toggle');
        // Give it a moment to turn off before updating
        setTimeout(() => performUpdate(wasRunning), 100);
    } else {
        performUpdate(false);
    }
}

function performUpdate(restartAfter) {
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

    console.log('Value updated');

    // If output was running before, restart it after a brief delay
    if (restartAfter) {
        setTimeout(() => {
            console.log('Restarting output after update...');
            websocket.send('toggle');
        }, 200);
    }
}

var selectedCard; // the acutal html element for the selected card
var selectedCardId; // the id of the selected card: 1-4
var selectedCardState = 'F'; // 'F' = Forward, 'R' = Reverse - 'F' is default

function selectCard(element){
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
    if(selectedCard == null) return;
    
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
    if(selectedCard == null) return;
    const inputValue = parseFloat(document.getElementById("slider").value);
    document.getElementById("new").value = inputValue;
}

function syncSlider(){
    if(selectedCard == null) return;
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

function downloadLogs() {
    console.log('Downloading logs...');
    const downloadButton = document.getElementById('download-logs-button');
    const originalText = downloadButton.innerHTML;

    // Show loading state
    downloadButton.innerHTML = '<span class="loading-spinner"></span> Preparing download...';
    downloadButton.disabled = true;

    // Trigger download
    window.location.href = '/downloadLogs';

    // Reset button after delay
    setTimeout(() => {
        downloadButton.innerHTML = originalText;
        downloadButton.disabled = false;
    }, 3000);
}

