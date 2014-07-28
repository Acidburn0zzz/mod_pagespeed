/*
 * Copyright 2014 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @fileoverview Code for adding auto-refreshing graphs and realtime linecharts
 * on the basis of the data in statistics page.
 *
 * TODO(xqyin): Integrate this into console page.
 *
 * @author oschaaf@we-amp.com (Otto van der Schaaf)
 * @author xqyin@google.com (XiaoQian Yin)
 */

goog.provide('pagespeed.Graphs');

goog.require('goog.array');
goog.require('goog.events');
goog.require('goog.net.EventType');
goog.require('goog.net.XhrIo');
goog.require('goog.string');

// Google Charts API.
// Requires <script src='https://www.google.com/jsapi'></script> loaded in HTML.
google.load('visualization', '1', {'packages': ['table', 'corechart']});


/** @typedef {{name: string, value:string}} */
pagespeed.StatsNode;


/** @typedef {{messages: Array.<pagespeed.StatsNode>, timeReceived: Date}} */
pagespeed.StatsArray;



/**
 * @constructor
 * @param {goog.testing.net.XhrIo=} opt_xhr Optional mock XmlHttpRequests
 *     handler for testing.
 */
pagespeed.Graphs = function(opt_xhr) {
  /**
   * The XmlHttpRequests handler for auto-refresh. We pass a mock XhrIo
   * when testing. Default is a normal XhrIo.
   * @private {goog.net.XhrIo|goog.testing.net.XhrIo}
   */
  this.xhr_ = opt_xhr || new goog.net.XhrIo();

  /**
   * This array of arrays collects historical data from statistics log file
   * for the first refresh and updates from back end to get snapshot of the
   * current data. The i-th entry of the array is the statistics corresponds
   * to the i-th time stamp, which is an array of special nodes. Each node
   * contains certain statistics name and the value. We use the last entry to
   * generate pie charts showing the most recent data. The whole array of
   * arrays is used to generate line charts showing statistics history. We only
   * keep one-day data to avoid infinite growth of this array.
   * @private {!Array.<pagespeed.StatsArray>}
   */
  this.psolMessages_ = [];

  /**
   * The option of auto-refresh. If true, the page will automatically refresh
   * itself. The default frequency is to refresh every 5 seconds.
   * @private {boolean}
   */
  this.autoRefresh_ = true;

  /**
   * The flag of whether the first refresh is started. We need to call a
   * refresh when loading the page for the initialization. Default is false,
   * which means the page hasn't started the first refresh yet.
   * @private {boolean}
   */
  this.firstRefreshStarted_ = false;

  /**
   * The flag of whether the second refresh is started. Since we fetch data
   * from statistics log file for the first refresh, the data may be stale.
   * We need to do the second refresh immediately to make sure the data we are
   * showing is up-to-date.
   * @private {boolean}
   */
  this.secondRefreshStarted_ = false;

  // The navigation bar to switch among different display modes
  // TODO(xqyin): Consider making these different tabs query params.
  var navElement = document.createElement('table');
  navElement.id = 'navBar';
  navElement.innerHTML =
      '<tr><td><a id="' + pagespeed.Graphs.DisplayMode.CACHE_APPLIED +
      '" href="javascript:void(0);">' +
      'Per application cache stats</a> - </td>' +
      '<td><a id="' + pagespeed.Graphs.DisplayMode.CACHE_TYPE +
      '" href="javascript:void(0);">' +
      'Per type cache stats</a> - </td>' +
      '<td><a id="' + pagespeed.Graphs.DisplayMode.IPRO +
      '" href="javascript:void(0);">' +
      'IPRO status</a> - </td>' +
      '<td><a id="' + pagespeed.Graphs.DisplayMode.REWRITE_IMAGE +
      '" href="javascript:void(0);">' +
      'Image rewriting</a> - </td>' +
      '<td><a id="' + pagespeed.Graphs.DisplayMode.REALTIME +
      '" href="javascript:void(0);">' +
      'Realtime</a></td></tr>';
  // The UI table of auto-refresh.
  var uiTable = document.createElement('div');
  uiTable.id = 'uiDiv';
  uiTable.innerHTML =
      '<table id="uiTable" border=1 style="border-collapse: ' +
      'collapse;border-color:silver;"><tr valign="center">' +
      '<td>Auto refresh (every 5 seconds): <input type="checkbox" ' +
      'id="autoRefresh" ' + (this.autoRefresh_ ? 'checked' : '') +
      '></td></tr></table>';
  document.body.insertBefore(
      uiTable,
      document.getElementById(pagespeed.Graphs.DisplayDiv.CACHE_APPLIED));
  document.body.insertBefore(navElement, document.getElementById('uiDiv'));
};


/**
 * Show the chosen div element and hide all other elements.
 * @param {string} div The chosen div element to display.
 */
pagespeed.Graphs.prototype.show = function(div) {
  // Hides all the elements first.
  document.getElementById(
      pagespeed.Graphs.DisplayDiv.CACHE_APPLIED).style.display = 'none';
  document.getElementById(
      pagespeed.Graphs.DisplayDiv.CACHE_TYPE).style.display = 'none';
  document.getElementById(
      pagespeed.Graphs.DisplayDiv.IPRO).style.display = 'none';
  document.getElementById(
      pagespeed.Graphs.DisplayDiv.REWRITE_IMAGE).style.display = 'none';
  document.getElementById(
      pagespeed.Graphs.DisplayDiv.REALTIME).style.display = 'none';
  // Only shows the element chosen by users.
  document.getElementById(div).style.display = '';
};


/**
 * The option of the display mode of the graphs page. Users can switch modes
 * by the second level navigation bar shown on the page. The page would show
 * the corresponding div elements containing charts.
 * @enum {string}
 */
pagespeed.Graphs.DisplayMode = {
  CACHE_APPLIED: 'cache_applied_mode',
  CACHE_TYPE: 'cache_type_mode',
  IPRO: 'ipro_mode',
  REWRITE_IMAGE: 'image_rewriting_mode',
  REALTIME: 'realtime_mode'
};


/**
 * The id of the div element that should be displayed in each mode. Only the
 * chosen div element would be shown. Others would be hidden.
 * @enum {string}
 */
pagespeed.Graphs.DisplayDiv = {
  CACHE_APPLIED: 'cache_applied',
  CACHE_TYPE: 'cache_type',
  IPRO: 'ipro',
  REWRITE_IMAGE: 'image_rewriting',
  REALTIME: 'realtime'
};


/**
 * Updates the option of auto-refresh.
 */
pagespeed.Graphs.prototype.toggleAutorefresh = function() {
  this.autoRefresh_ = !this.autoRefresh_;
};


/**
 * Generate a URL to request statistics data over default time period.
 * @return {string} The query URL incorporating all time parameters.
 */
pagespeed.Graphs.prototype.createQueryUrl = function() {
  var granularityMs = pagespeed.Graphs.FREQUENCY_ * 1000;
  var durationMs = pagespeed.Graphs.TIMERANGE_ * granularityMs; // 1 Day.
  var endTime = new Date();
  var startTime = new Date(endTime - durationMs);
  var queryString = '?json';
  queryString += '&start_time=' + startTime.getTime();
  queryString += '&end_time=' + endTime.getTime();
  queryString += '&granularity=' + granularityMs;
  return queryString;
};


/**
 * Parses historical data sent by sever.
 * @param {string} text The statistics dumped in JSON format.
 */
pagespeed.Graphs.prototype.parseHistoricalMessages = function(text) {
  var jsonData = JSON.parse(text);
  var timestamps = jsonData['timestamps'];
  var variables = jsonData['variables'];
  for (var i = 0; i < timestamps.length; ++i) {
    var messages = [];
    for (var name in variables) {
      var node = {
        name: name,
        value: variables[name][i]
      };
      messages.push(node);
    }
    var newArray = {
      messages: messages,
      timeReceived: new Date(timestamps[i])
    };
    this.psolMessages_.push(newArray);
  }
};


/**
 * Parses the most recent data sent by sever.
 * @param {string} text The statistics dumped in JSON format.
 */
pagespeed.Graphs.prototype.parseSnapshotMessages = function(text) {
  var jsonData = JSON.parse(text);
  var variables = jsonData['variables'];
  var messages = [];
  for (var name in variables) {
    var node = {
      name: name,
      value: variables[name]
    };
    messages.push(node);
  }
  var newArray = {
    messages: messages,
    timeReceived: new Date()
  };
  this.psolMessages_.push(newArray);
};


/**
 * Capture the pathname and return the URL for request.
 * @return {string} The URL to request the updated data in JSON format.
 */
pagespeed.Graphs.prototype.requestUrl = function() {
  var pathName = location.pathname;
  var n = pathName.lastIndexOf('/');
  // Ignore the ending '/'.
  if (n == pathName.length - 1) {
    n = pathName.substring(0, n).lastIndexOf('/');
  }
  if (n > 0) {
    return pathName.substring(0, n) + '/stats_json';
  } else {
    return '/stats_json';
  }
};


/**
 * Refreshes the page by making requsts to server.
 */
pagespeed.Graphs.prototype.performRefresh = function() {
  // TODO(xqyin): Figure out if there is a potential timing issue that the
  // xhr.isActive() would be false before the callback starts.
  if (!this.xhr_.isActive()) {
    // If the first refresh has not started yet, then do the refresh no matter
    // what the autoRefresh option is. Because the page needs to send a query
    // URL to request historical data to get initialized.
    if (!this.firstRefreshStarted_) {
      this.firstRefreshStarted_ = true;
      this.xhr_.send(this.createQueryUrl());
    // The page needs to do the second refresh to finish initialization.
    // Otherwise, only refresh when the autoRefresh option is true.
    } else if (!this.secondRefreshStarted_ || this.autoRefresh_) {
      this.secondRefreshStarted_ = true;
      this.xhr_.send(this.requestUrl());
    }
  }
};


/**
 * Parses the response sent by server and draws charts.
 */
pagespeed.Graphs.prototype.parseAjaxResponse = function() {
  if (this.xhr_.isSuccess()) {
    var text = this.xhr_.getResponseText();
    if (!this.secondRefreshStarted_) {
      // We collect historical data in the first refresh and wait for the
      // second refresh to update data. We won't call functions to draw charts
      // until we make data up-to-date.
      this.parseHistoricalMessages(text);
      // Add the second refresh to the queue with no delay time. So it would be
      // implemented immediately after the first refresh completion.
      window.setTimeout(goog.bind(this.performRefresh, this), 0);
    } else {
      this.parseSnapshotMessages(text);
      // Only keep one-day statistics.
      if (this.psolMessages_.length > pagespeed.Graphs.TIMERANGE_) {
        this.psolMessages_.shift();
      }
      this.drawVisualization();
    }
  } else {
    console.log(this.xhr_.getLastError());
  }
};


/**
 * Initialization for drawing all the charts.
 */
pagespeed.Graphs.prototype.drawVisualization = function() {
  var prefixes = [
    ['pcache-cohorts-dom_', 'Property cache dom cohorts', 'PieChart',
     pagespeed.Graphs.DisplayDiv.CACHE_APPLIED],
    ['pcache-cohorts-beacon_', 'Property cache beacon cohorts', 'PieChart',
     pagespeed.Graphs.DisplayDiv.CACHE_APPLIED],
    ['rewrite_cached_output_', 'Rewrite cached output', 'PieChart',
     pagespeed.Graphs.DisplayDiv.CACHE_APPLIED],
    ['url_input_', 'URL Input', 'PieChart',
     pagespeed.Graphs.DisplayDiv.CACHE_APPLIED],

    ['cache_', 'Cache', 'PieChart', pagespeed.Graphs.DisplayDiv.CACHE_TYPE],
    ['file_cache_', 'File Cache', 'PieChart',
     pagespeed.Graphs.DisplayDiv.CACHE_TYPE],
    ['memcached_', 'Memcached', 'PieChart',
     pagespeed.Graphs.DisplayDiv.CACHE_TYPE],
    ['lru_cache_', 'LRU', 'PieChart', pagespeed.Graphs.DisplayDiv.CACHE_TYPE],
    ['shm_cache_', 'Shared Memory', 'PieChart',
     pagespeed.Graphs.DisplayDiv.CACHE_TYPE],

    ['ipro_', 'In place resource optimization', 'PieChart',
     pagespeed.Graphs.DisplayDiv.IPRO],

    ['image_rewrite_', 'Image rewrite', 'PieChart',
     pagespeed.Graphs.DisplayDiv.REWRITE_IMAGE],
    ['image_rewrites_dropped_', 'Image rewrites dropped', 'PieChart',
     pagespeed.Graphs.DisplayDiv.REWRITE_IMAGE],

    ['http_', 'Http', 'LineChart', pagespeed.Graphs.DisplayDiv.REALTIME, true],
    ['file_cache_', 'File Cache RT', 'LineChart',
     pagespeed.Graphs.DisplayDiv.REALTIME, true],
    ['lru_cache_', 'LRU Cache RT', 'LineChart',
     pagespeed.Graphs.DisplayDiv.REALTIME, true],
    ['serf_fetch_', 'Serf stats RT', 'LineChart',
     pagespeed.Graphs.DisplayDiv.REALTIME, true],
    ['rewrite_', 'Rewrite stats RT', 'LineChart',
     pagespeed.Graphs.DisplayDiv.REALTIME, true]
  ];

  for (var i = 0; i < prefixes.length; ++i) {
    this.drawChart(prefixes[i][0], prefixes[i][1], prefixes[i][2],
                   prefixes[i][3], prefixes[i][4]);
  }
};


/**
 * Screens data to generate charts according to the setting prefix.
 * @param {string} prefix The setting prefix to match.
 * @param {string} name The name of the statistics.
 * @return {boolean} Return true if the data should be used in the chart.
 */
pagespeed.Graphs.screenData = function(prefix, name) {
  var use = true;
  if (name.indexOf(prefix) != 0) {
    use = false;
  // We skip here because the statistics below won't be used in any charts.
  } else if (name.indexOf('cache_flush_timestamp_ms') >= 0) {
    use = false;
  } else if (name.indexOf('cache_flush_count') >= 0) {
    use = false;
  } else if (name.indexOf('cache_time_us') >= 0) {
    use = false;
  }
  return use;
};


/**
 * Draw the chart using Google Charts API.
 * @param {string} settingPrefix The matching prefix of data for the chart.
 * @param {string} title The title of the chart.
 * @param {string} chartType The type of the chart. LineChart or PieChart.
 * @param {string} targetId The id of the target HTML element.
 * @param {boolean} showHistory The flag of history line charts.
 */
pagespeed.Graphs.prototype.drawChart = function(settingPrefix, title,
                                                chartType, targetId,
                                                showHistory) {
  this.drawChart.chartCache = this.drawChart.chartCache ?
                              this.drawChart.chartCache : {};
  var theChart;
  // TODO(oschaaf): Title might not be unique
  if (this.drawChart.chartCache[title]) {
    theChart = this.drawChart.chartCache[title];
  } else {
    // The element identified by the id must exist.
    var targetElement = document.getElementById(targetId);
    // Remove the status info when it starts to draw charts.
    if (targetElement.innerText == 'Loading Charts...') {
      targetElement.innerText = '';
    }
    var dest = document.createElement('div');
    dest.className = 'chart';
    targetElement.appendChild(dest);
    theChart = new google.visualization[chartType](dest);
    this.drawChart.chartCache[title] = theChart;
  }

  var rows = [];
  var data = new google.visualization.DataTable();

  // The graphs for recentest data.
  if (!showHistory) {
    var messages = goog.array.clone(
        this.psolMessages_[this.psolMessages_.length - 1].messages);
    for (var i = 0; i < messages.length; ++i) {
      if (messages[i].value == '0') continue;
      if (!pagespeed.Graphs.screenData(settingPrefix, messages[i].name)) {
        continue;
      }
      // Removes the prefix.
      var caption = messages[i].name.substring(settingPrefix.length);
      // We use regexp here to replace underscores all at once.
      // Using '_' would only replace one underscore at a time.
      caption = caption.replace(/_/ig, ' ');
      rows.push([caption, Number(messages[i].value)]);
    }
    data.addColumn('string', 'Name');
    data.addColumn('number', 'Value');
  } else {
    // The line charts for data history.
    data.addColumn('datetime', 'Time');
    var first = true;
    for (var i = 0; i < this.psolMessages_.length; ++i) {
      var messages = goog.array.clone(this.psolMessages_[i].messages);
      var row = [];
      row.push(this.psolMessages_[i].timeReceived);
      for (var j = 0; j < messages.length; ++j) {
        if (!pagespeed.Graphs.screenData(settingPrefix, messages[j].name)) {
          continue;
        }
        row.push(Number(messages[j].value));
        if (first) {
          var caption = messages[j].name.substring(settingPrefix.length);
          caption = caption.replace(/_/ig, ' ');
          data.addColumn('number', caption);
        }
      }
      first = false;
      rows.push(row);
    }
  }

  // TODO(oschaaf): Merge this with options from an argument or some such.
  var options = {
    'width': 1000,
    'height': 300,
    'chartArea': {
      'left': 100,
      'top': 50,
      'width': 700
    },
    title: title
  };
  data.addRows(rows);
  theChart.draw(data, options);
};


/**
 * The frequency of auto-refresh. Default is once per 5 seconds.
 * @private {number}
 * @const
 */
pagespeed.Graphs.FREQUENCY_ = 5;


/**
 * The size limit to the data array. Default is one day.
 * @private {number}
 * @const
 */
pagespeed.Graphs.TIMERANGE_ = 24 * 60 * 60 /
                                  pagespeed.Graphs.FREQUENCY_;


/**
 * The Main entry to start processing.
 * @export
 */
pagespeed.Graphs.Start = function() {
  var graphsOnload = function() {
    var graphsObj = new pagespeed.Graphs();
    goog.events.listen(document.getElementById('autoRefresh'), 'change',
                       goog.bind(graphsObj.toggleAutorefresh,
                                 graphsObj));
    goog.events.listen(
        document.getElementById(pagespeed.Graphs.DisplayMode.CACHE_APPLIED),
        'click',
        goog.bind(graphsObj.show, graphsObj,
                  pagespeed.Graphs.DisplayDiv.CACHE_APPLIED));
    goog.events.listen(
        document.getElementById(pagespeed.Graphs.DisplayMode.CACHE_TYPE),
        'click',
        goog.bind(graphsObj.show, graphsObj,
                  pagespeed.Graphs.DisplayDiv.CACHE_TYPE));
    goog.events.listen(
        document.getElementById(pagespeed.Graphs.DisplayMode.IPRO), 'click',
        goog.bind(graphsObj.show, graphsObj,
                  pagespeed.Graphs.DisplayDiv.IPRO));
    goog.events.listen(
        document.getElementById(pagespeed.Graphs.DisplayMode.REWRITE_IMAGE),
        'click',
        goog.bind(graphsObj.show, graphsObj,
                  pagespeed.Graphs.DisplayDiv.REWRITE_IMAGE));
    goog.events.listen(
        document.getElementById(pagespeed.Graphs.DisplayMode.REALTIME),
        'click',
        goog.bind(graphsObj.show, graphsObj,
                  pagespeed.Graphs.DisplayDiv.REALTIME));
    // We call listen() here so this listener is added to the xhr only once.
    // If we call listen() inside performRefresh() method, we are adding a new
    // listener to the xhr every time it auto-refreshes, which would cause
    // fetchContent() being called multiple times. Users will see an obvious
    // delay because we draw the same charts multiple times in one refresh.
    goog.events.listen(
        graphsObj.xhr_, goog.net.EventType.COMPLETE,
        goog.bind(graphsObj.parseAjaxResponse, graphsObj));
    setInterval(graphsObj.performRefresh.bind(graphsObj),
                pagespeed.Graphs.FREQUENCY_ * 1000);
    graphsObj.performRefresh();
  };
  goog.events.listen(window, 'load', graphsOnload);
};
