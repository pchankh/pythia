var server = "http://" + window.location.hostname + ":" + window.location.port;

function urlFormat(q) {
  return q.replace(/ /g, '+');
}

function userFormat(q) {
  return q.replace(/\+/g, ' ');
}

function userQuery(q) {
  if (q) {
    document.getElementById("query").value = q.toLowerCase(); 
  }
  return document.getElementById('query').value; 
}

function urlQuery(q) {
  if (q) {
    window.location.search = '?q=' + q.toLowerCase();
  }
  return window.location.search.substr(3);
}

function search() {
  userQuery(userFormat(urlQuery()));
  $.ajax({url: server + "/",
    data: "qf=" + urlQuery(),
    dataType: "json",
    success: callback});
}

function callback(data, status, xhr) {
  var view_left = "";
  for (var i in data.results.items) {
    var title = data.results.items[i]["htmlTitle"];
    var snippet = data.results.items[i]["snippet"];
    var link = data.results.items[i]["link"];
    var link_name = data.results.items[i]["displayLink"]
    var element = "<p>" + "<h2>" + title  + "</h2>"
        + snippet + "<br />"
        + "<a href=\"" + link + "\">" + link_name + "</a></p>";
    view_left += element;
  }
  if (data.results.items.length == 0) {
    view_left += "<h2>no results</h2>";
  }
  $("#view-left").html(view_left);

  var meta_result1 = "<div id=\"meta-result-area1\">";
  for (var i in data.target_keywords) {
    var keyword = data.target_keywords[i];
    var element = "<h2>" + keyword + "</h2>";
    if (i > 0) {
      meta_result1 += ", ";
    }
    meta_result1 += element;
  }
  meta_result1 += " <span style=\"font-size: 0.61em;\">/</span> " +
                  data.target_type + "</div>";
  $("#meta-result-area1").replaceWith(meta_result1);

  var meta_result2 = "<div id=\"meta-result-area2\">";
  for (var i in data.entities) {
    var name = data.entities[i][0];
    var rank = data.entities[i][1];
    var element = "<h2>" + name + "</h2>:" + rank + "   ";
    meta_result2 += element;
  }
  if (data.entities.length == 0) {
    meta_result2 += "no entities found";
  }
  meta_result2 += "</div>";
  $("#meta-result-area2").replaceWith(meta_result2);
}

$(document).ready (
  function() {
    if (window.location.pathname == "/index.html") {
      window.location.pathname = "";
    }
    if (urlQuery()) {
      search();
    }
  }
);

$(document).keypress (
  function(event) {
    if (event.which == 13) {
      event.preventDefault();
      urlQuery(urlFormat(userQuery()));
    }
  }
);
