function validHex(str) {
    return /^(0[xX]){0,1}[A-Fa-f0-9]+$/.test(str);
}

function validPath(str) {
    return /[^\0]+/.test(str);
}

toastr.options = {
    "closeButton": false,
    "debug": false,
    "newestOnTop": true,
    "progressBar": false,
    "positionClass": "toast-bottom-center",
    "preventDuplicates": true,
    "onclick": null,
    "showDuration": "300",
    "hideDuration": "500",
    "timeOut": "1500",
    "extendedTimeOut": "1000",
    "showEasing": "swing",
    "hideEasing": "linear",
    "showMethod": "fadeIn",
    "hideMethod": "fadeOut"
};

var filter = [];
var favorites = [];
var folders = {};
var j = null;

window.onload = () => {
    $.ajax({
        url: '/populate',
        method: 'GET',
        dataType: 'html',
        success: (data) => {
            try {
                j = JSON.parse(data);
                document.getElementById("enable-pksm-bridge").checked = j["pksm-bridge"];
                j['favorites'].forEach((id) => {
                    pushToFavorites(id);
                });
                j['filter'].forEach((id) => {
                    pushToFilter(id);
                });
                createAdditionalSavesRows(j["title_list"]);
                Object.keys(j["additional_save_folders"]).forEach((id) => {
                    j.additional_save_folders[id].folders.forEach((path) => {
                        pushToFolders(id, path);
                    });
                });
            } catch (err) {
                console.log(err);
                toastr["error"]("", "Failed to retrieve configurations!");
            };
        },
        error: (data) => {
            toastr["error"]("", "Failed to populate configuration page!");
        }
    });
};

function createRow(id, value) {
    var ul = document.getElementById(id);
    var li = document.createElement("li");
    var div = document.createElement("div");
    var h6 = document.createElement("h6");
    li.setAttribute("class", "list-group-item d-flex justify-content-between lh-condensed");
    h6.setAttribute("class", "my-0");
    h6.appendChild(document.createTextNode(value));
    div.appendChild(h6);
    li.appendChild(div);
    ul.appendChild(li);
}

function createAdditionalSavesRows(j) {
    Object.keys(j).forEach((i) => {
        var father = document.getElementById("additional-saves-list");
        var div1 = document.createElement("div");
        var h5 = document.createElement("h5");
        var a = document.createElement("a");
        div1.setAttribute("class", "panel-heading");
        h5.setAttribute("class", "panel-title");
        a.setAttribute("class", "list-group-item d-flex justify-content-between lh-condensed");
        a.setAttribute("data-toggle", "collapse");
        a.setAttribute("data-parent", "#accordion");
        a.setAttribute("href", `#collapse-${i}`);
        a.appendChild(document.createTextNode(j[i]));
        div1.appendChild(h5);
        h5.appendChild(a);
        father.appendChild(div1);

        var div2 = document.createElement("div");
        var div3 = document.createElement("div");
        var ul = document.createElement("ul");
        var div4 = document.createElement("div");
        var input = document.createElement("input");
        var div5 = document.createElement("div");
        var button = document.createElement("button");
        div2.setAttribute("id", `collapse-${i}`);
        div2.setAttribute("class", "panel-collapse collapse in");
        div3.setAttribute("class", "card card-body");
        ul.setAttribute("id", `ul-${i}`);
        div4.setAttribute("class", "input-group");
        input.setAttribute("id", `input-${i}`);
        input.setAttribute("type", "text");
        input.setAttribute("class", "form-control");
        input.setAttribute("placeholder", "Additional folder");
        div5.setAttribute("class", "input-group-append");
        button.setAttribute("type", "submit");
        button.setAttribute("onclick", `pushToFolders('${i}');`);
        button.setAttribute("class", "btn btn-primary");
        button.appendChild(document.createTextNode("Submit"));
        div2.appendChild(div3);
        div3.appendChild(ul);
        div3.appendChild(div4);
        div4.appendChild(input);
        div4.appendChild(div5);
        div5.appendChild(button);
        father.appendChild(div2);
    });
}

function saveSettings() {
    var data = {
        'pksm-bridge': document.getElementById("enable-pksm-bridge").checked,
        'filter': filter,
        'favorites': favorites,
        'additional_save_folders': {},
        'version': 3
    };
    Object.keys(j.title_list).forEach((id) => {
        data.additional_save_folders[id] = {};
        data.additional_save_folders[id].folders = [];
        Array.from(document.querySelectorAll(`#ul-${id}>li>div`), (path) => {
            data.additional_save_folders[id].folders.push(path.textContent);
        });
    });
    console.log(JSON.stringify(data));
    $.ajax({
        url: '/save',
        method: 'POST',
        dataType: 'html',
        data: JSON.stringify(data),
        success: (data) => {
            toastr["success"]("", "Saved!");
        },
        error: (data) => {
            toastr["error"]("", "Failed to send request!");
        }
    });
}

function pushToFilter(id) {
    var value = id || document.getElementById("input-filter").value;
    if (value) {
        if (validHex(value)) {
            filter.push(value);
            createRow("filter-list", value);
            document.getElementById("input-filter").value = "";
            document.getElementById("filter-badge").innerText = document.getElementById("filter-list").getElementsByTagName("li").length.toString();
        } else {
            toastr["warning"]("", "Failed to push id!");
        }
    }
}

function pushToFavorites(id) {
    var value = id || document.getElementById("input-favorites").value;
    if (value) {
        if (validHex(value)) {
            favorites.push(value);
            createRow("favorites-list", value);
            document.getElementById("input-favorites").value = "";
            document.getElementById("favorites-badge").innerText = document.getElementById("favorites-list").getElementsByTagName("li").length.toString();
        } else {
            toastr["warning"]("", "Failed to push id!");
        }
    }
}

function pushToFolders(id, path) {
    var value = path || document.getElementById(`input-${id}`).value;
    if (value) {
        if (validPath(value)) {
            if (folders.id == null) {
                folders.id = [];
            }
            folders.id.push(value);
            createRow(`ul-${id}`, value);
            document.getElementById(`input-${id}`).value = "";
        } else {
            toastr["warning"]("", "Failed to push path!");
        }
    }
}